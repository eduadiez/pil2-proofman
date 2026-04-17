#include "expressions_ctx.hpp"
#include "expressions_pack.hpp"
#include "hints.hpp"

// ----------------------------------------------------------------------------
// Parallel prefix scan helper.
//
// `accHintField` / `accMulHintFields` both do `vals[i] = vals[i] OP vals[i-1]`
// over N = 1<<nBits rows serially. That's a serial dependency chain that
// leaves 9+ P-cores idle. Both `+` and `*` are associative, so classic
// three-phase parallel prefix applies:
//   1. Each thread does an in-place inclusive scan of its block.
//   2. Scan block totals serially (P is small, ~10 on M4 Pro).
//   3. Each thread (except block 0) combines its block's exclusive prefix
//      into every element of its block.
//
// Works for base-field (stride=1) and cubic (stride=3). Small-N fallback
// to serial keeps correctness on tiny domains without OMP overhead.
// ----------------------------------------------------------------------------
namespace {

static inline void prefix_op_apply(Goldilocks::Element* dst,
                                   const Goldilocks::Element* a,
                                   const Goldilocks::Element* b,
                                   uint64_t stride, bool is_add) {
    if (stride == 1) {
        if (is_add) Goldilocks::add(*dst, *a, *b);
        else        Goldilocks::mul(*dst, *a, *b);
    } else {
        auto& d  = *reinterpret_cast<Goldilocks3::Element*>(dst);
        auto& aa = *reinterpret_cast<const Goldilocks3::Element*>(a);
        auto& bb = *reinterpret_cast<const Goldilocks3::Element*>(b);
        if (is_add) Goldilocks3::add(d, aa, bb);
        else        Goldilocks3::mul(d, aa, bb);
    }
}

static inline void prefix_op_identity(Goldilocks::Element* dst,
                                      uint64_t stride, bool is_add) {
    if (stride == 1) {
        dst[0] = is_add ? Goldilocks::zero() : Goldilocks::one();
    } else {
        auto& d = *reinterpret_cast<Goldilocks3::Element*>(dst);
        if (is_add) Goldilocks3::zero(d);
        else        Goldilocks3::one(d);
    }
}

static void prefix_scan_inplace(Goldilocks::Element* vals,
                                uint64_t N, uint64_t stride, bool is_add) {
    if (N <= 1) return;

    // Serial fallback for tiny N where OMP overhead dominates.
    // Empirically on fibonacci-square e2e, N=2^16 (=65536) per call loses
    // ~1ms to OMP dispatch vs ~0.2ms saved by parallelism — net regression.
    // Threshold of 2^18 keeps parallel path on for circuits with ≥256K rows
    // (the regime where the 5-20% Codex estimate actually materialises).
    constexpr uint64_t kMinParallelN = 1u << 18;
    int P = omp_get_max_threads();
    if (N < kMinParallelN || P <= 1) {
        for (uint64_t i = 1; i < N; ++i) {
            prefix_op_apply(&vals[i*stride], &vals[i*stride],
                            &vals[(i-1)*stride], stride, is_add);
        }
        return;
    }

    if ((uint64_t)P > N) P = (int)N;
    uint64_t chunk = (N + P - 1) / P;

    // Phase 1: per-thread local inclusive scan.
    std::vector<Goldilocks::Element> block_last((size_t)P * stride);
  #pragma omp parallel for num_threads(P) schedule(static, 1)
    for (int t = 0; t < P; ++t) {
        uint64_t s = (uint64_t)t * chunk;
        uint64_t e = std::min(s + chunk, N);
        if (s >= e) continue;
        for (uint64_t i = s + 1; i < e; ++i) {
            prefix_op_apply(&vals[i*stride], &vals[i*stride],
                            &vals[(i-1)*stride], stride, is_add);
        }
        for (uint64_t d = 0; d < stride; ++d) {
            block_last[(size_t)t*stride + d] = vals[(e-1)*stride + d];
        }
    }

    // Phase 2: serial scan of block totals into exclusive prefixes.
    std::vector<Goldilocks::Element> block_prefix((size_t)P * stride);
    prefix_op_identity(&block_prefix[0], stride, is_add);
    for (int t = 1; t < P; ++t) {
        prefix_op_apply(&block_prefix[(size_t)t*stride],
                        &block_prefix[(size_t)(t-1)*stride],
                        &block_last[(size_t)(t-1)*stride],
                        stride, is_add);
    }

    // Phase 3: combine block's exclusive prefix into each of its elements.
    // Block 0's prefix is identity, so skip it.
  #pragma omp parallel for num_threads(P) schedule(static, 1)
    for (int t = 1; t < P; ++t) {
        uint64_t s = (uint64_t)t * chunk;
        uint64_t e = std::min(s + chunk, N);
        if (s >= e) continue;
        for (uint64_t i = s; i < e; ++i) {
            prefix_op_apply(&vals[i*stride], &vals[i*stride],
                            &block_prefix[(size_t)t*stride],
                            stride, is_add);
        }
    }
}

}  // anonymous namespace


void getPolynomial(SetupCtx& setupCtx, Goldilocks::Element *buffer, Goldilocks::Element *dest, PolMap& polInfo, uint64_t rowOffsetIndex, string type) {
    std::string stage = type == "cm" ? "cm" + to_string(polInfo.stage) : type == "custom" ? setupCtx.starkInfo.customCommits[polInfo.commitId].name + "0" : "const";
    Goldilocks::Element *buff = &buffer[polInfo.stagePos];
    uint64_t deg = 1 << setupCtx.starkInfo.starkStruct.nBits;
    uint64_t rowOffset = setupCtx.starkInfo.openingPoints[rowOffsetIndex];
    uint64_t nCols = setupCtx.starkInfo.mapSectionsN[stage];
    uint64_t dim = polInfo.dim;
#pragma omp parallel for
    for(uint64_t j = 0; j < deg; ++j) {
        uint64_t l = (j + rowOffset)%deg;
        std::memcpy(&dest[j*dim], &buff[l*nCols], dim * sizeof(Goldilocks::Element));
    }
}

void setPolynomial(SetupCtx& setupCtx, Goldilocks::Element *buffer, Goldilocks::Element *values, uint64_t idPol) {
    PolMap polInfo = setupCtx.starkInfo.cmPolsMap[idPol];
    uint64_t deg = 1 << setupCtx.starkInfo.starkStruct.nBits;
    uint64_t dim = polInfo.dim;
    std::string stage = "cm" + to_string(polInfo.stage);
    uint64_t nCols = setupCtx.starkInfo.mapSectionsN[stage];
    uint64_t offset = setupCtx.starkInfo.mapOffsets[std::make_pair(stage, false)];
    offset += polInfo.stagePos;
    Goldilocks::Element *buff = &buffer[offset];
#pragma omp parallel for
    for(uint64_t j = 0; j < deg; ++j) {
        std::memcpy(&buff[j*nCols], &values[j*dim], dim * sizeof(Goldilocks::Element));
    }
}

void printRow(SetupCtx& setupCtx, Goldilocks::Element* buffer, uint64_t stage, uint64_t row) {
    Goldilocks::Element *pol = &buffer[setupCtx.starkInfo.mapOffsets[std::make_pair("cm" + to_string(stage), false)] + setupCtx.starkInfo.mapSectionsN["cm" + to_string(stage)] * row];
    cout << "Values at row " << row << " = {" << endl;
    bool first = true;
    for(uint64_t i = 0; i < setupCtx.starkInfo.cmPolsMap.size(); ++i) {
        PolMap cmPol = setupCtx.starkInfo.cmPolsMap[i];
        if(cmPol.stage == stage) {
            if(first) {
                first = false;
            } else {
                cout << endl;
            }
            cout << "    " << cmPol.name;
            if(cmPol.lengths.size() > 0) {
                cout << "[";
                for(uint64_t i = 0; i < cmPol.lengths.size(); ++i) {
                    cout << cmPol.lengths[i];
                    if(i != cmPol.lengths.size() - 1) cout << ", ";
                }
                cout << "]";
            }
            cout << ": ";
            if(cmPol.dim == 1) {
                cout << Goldilocks::toString(pol[cmPol.stagePos]) << ",";
            } else {
                cout << "[" << Goldilocks::toString(pol[cmPol.stagePos]) << ", " << Goldilocks::toString(pol[cmPol.stagePos + 1]) << ", " << Goldilocks::toString(pol[cmPol.stagePos + 2]) << "],";
            }
        }
    }
    cout << endl;
    cout << "}" << endl;
}

std::string getExpressionDebug(SetupCtx& setupCtx, uint64_t hintId, std::string hintFieldName, HintFieldValue hintFieldVal) {
    std::string debug = "Hint name " + hintFieldName + " for hint id " + to_string(hintId) + " is ";
    
    if(hintFieldVal.operand == opType::cm) {
        debug += "witness col " + setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].name;
        if(setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].lengths.size() > 0) {
            debug +=  "[";
            for(uint64_t i = 0; i < setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].lengths.size(); ++i) {
                debug += to_string(setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].lengths[i]);
                if(i != setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].lengths.size() - 1) debug += ", ";
            }
            debug += "]";
        }
    } else if(hintFieldVal.operand == opType::custom) {
        debug += "custom col " + setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].name;
        if(setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].lengths.size() > 0) {
            debug += "[";
            for(uint64_t i = 0; i < setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].lengths.size(); ++i) {
                debug += to_string(setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].lengths[i]);
                if(i != setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].lengths.size() - 1) debug += ", ";
            }
            debug += "]";
        }
    } else if(hintFieldVal.operand == opType::const_) {
        debug += "fixed col" + setupCtx.starkInfo.constPolsMap[hintFieldVal.id].name;
        if(setupCtx.starkInfo.constPolsMap[hintFieldVal.id].lengths.size() > 0) {
            debug += "[";
            for(uint64_t i = 0; i < setupCtx.starkInfo.constPolsMap[hintFieldVal.id].lengths.size(); ++i) {
                debug += to_string(setupCtx.starkInfo.constPolsMap[hintFieldVal.id].lengths[i]);
                if(i != setupCtx.starkInfo.constPolsMap[hintFieldVal.id].lengths.size() - 1) debug += ", ";
            }
            debug += "]";
        }
    } else if (hintFieldVal.operand == opType::tmp) {
        debug += "the expression with id: ";
        if(setupCtx.expressionsBin.expressionsInfo[hintFieldVal.id].line != "") {
            debug += " " + setupCtx.expressionsBin.expressionsInfo[hintFieldVal.id].line;
        }
    } else if (hintFieldVal.operand == opType::public_) {
        debug += "public input " + setupCtx.starkInfo.publicsMap[hintFieldVal.id].name;
    } else if (hintFieldVal.operand == opType::proofvalue) {
        debug += "proof value  " + setupCtx.starkInfo.proofValuesMap[hintFieldVal.id].name;
    } else if (hintFieldVal.operand == opType::number) {
        debug += "number " + to_string(hintFieldVal.value);
    } else if (hintFieldVal.operand == opType::airgroupvalue) {
        debug += "airgroupValue " + setupCtx.starkInfo.airgroupValuesMap[hintFieldVal.id].name;
    } else if (hintFieldVal.operand == opType::airvalue) {
        debug += "airValue " + setupCtx.starkInfo.airValuesMap[hintFieldVal.id].name;
    } else if (hintFieldVal.operand == opType::challenge) {
        debug += "challenge " + setupCtx.starkInfo.challengesMap[hintFieldVal.id].name;
    } else if (hintFieldVal.operand == opType::string_) {
        debug += "string " + hintFieldVal.stringValue;
    } else {
        zklog.error("Unknown HintFieldType");
        exitProcess();
        exit(-1);
    }

    return debug;
}

uint64_t getHintFieldValues(SetupCtx& setupCtx, uint64_t hintId, std::string hintFieldName) {
     Hint hint = setupCtx.expressionsBin.hints[hintId];
    
    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldName](const HintField& hintField) {
        return hintField.name == hintFieldName;
    });

    if(hintField == hint.fields.end()) {
        zklog.error("Hint field " + hintFieldName + " not found in hint " + hint.name + ".");
        exitProcess();
        exit(-1);
    }

    return hintField->values.size();
}

void getHintFieldSizes(
    SetupCtx& setupCtx, 
    HintFieldInfo *hintFieldValues,
    uint64_t hintId, 
    std::string hintFieldName,
    HintFieldOptions& hintOptions
) {

    uint64_t deg = 1 << setupCtx.starkInfo.starkStruct.nBits;

    if(setupCtx.expressionsBin.hints.size() == 0) {
        zklog.error("No hints were found.");
        exitProcess();
        exit(-1);
    }

    Hint hint = setupCtx.expressionsBin.hints[hintId];
    
    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldName](const HintField& hintField) {
        return hintField.name == hintFieldName;
    });

    if(hintField == hint.fields.end()) {
        zklog.error("Hint field " + hintFieldName + " not found in hint " + hint.name + ".");
        exitProcess();
        exit(-1);
    }

    for(uint64_t i = 0; i < hintField->values.size(); ++i) {
        HintFieldValue hintFieldVal = hintField->values[i];

        if(hintOptions.print_expression) {
            std::string expression_line = getExpressionDebug(setupCtx, hintId, hintFieldName, hintFieldVal);
            hintFieldValues[i].expression_line_size = expression_line.size();
        }
        if(hintFieldVal.operand == opType::cm) {
            uint64_t dim = setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].dim;
            hintFieldValues[i].size = deg*dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Column : HintFieldType::ColumnExtended;
            hintFieldValues[i].offset = dim;
        } else if(hintFieldVal.operand == opType::custom) {
            uint64_t dim = setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id].dim;
            hintFieldValues[i].size = deg*dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Column : HintFieldType::ColumnExtended;
            hintFieldValues[i].offset = dim;
        } else if(hintFieldVal.operand == opType::const_) {
            uint64_t dim = setupCtx.starkInfo.constPolsMap[hintFieldVal.id].dim;
            hintFieldValues[i].size = deg*dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Column : HintFieldType::ColumnExtended;
            hintFieldValues[i].offset = dim;
        } else if (hintFieldVal.operand == opType::tmp) {
            if(hintOptions.compilation_time) {
                hintFieldValues[i].size = 1;
                hintFieldValues[i].fieldType = HintFieldType::Field;
                hintFieldValues[i].offset = 1;
            } else {
                uint64_t dim = setupCtx.expressionsBin.expressionsInfo[hintFieldVal.id].destDim;
                hintFieldValues[i].size = deg*dim;
                hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Column : HintFieldType::ColumnExtended;
                hintFieldValues[i].offset = dim;
            }
        } else if (hintFieldVal.operand == opType::public_) {
            hintFieldValues[i].size = 1;
            hintFieldValues[i].fieldType = HintFieldType::Field;
            hintFieldValues[i].offset = 1;
        } else if (hintFieldVal.operand == opType::number) {
            hintFieldValues[i].size = 1;
            hintFieldValues[i].fieldType = HintFieldType::Field;
            hintFieldValues[i].offset = 1;
        } else if (hintFieldVal.operand == opType::airgroupvalue) {
            uint64_t dim = setupCtx.starkInfo.airgroupValuesMap[hintFieldVal.id].stage == 1 ? 1 : FIELD_EXTENSION;
            hintFieldValues[i].size = dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Field : HintFieldType::FieldExtended;
            hintFieldValues[i].offset = dim;
        } else if (hintFieldVal.operand == opType::airvalue) {
            uint64_t dim = setupCtx.starkInfo.airValuesMap[hintFieldVal.id].stage == 1 ? 1 : FIELD_EXTENSION;
            hintFieldValues[i].size = dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Field : HintFieldType::FieldExtended;
            hintFieldValues[i].offset = dim;
        } else if (hintFieldVal.operand == opType::proofvalue) {
            uint64_t dim = setupCtx.starkInfo.proofValuesMap[hintFieldVal.id].stage == 1 ? 1 : FIELD_EXTENSION;
            hintFieldValues[i].size = dim;
            hintFieldValues[i].fieldType = dim == 1 ? HintFieldType::Field : HintFieldType::FieldExtended;
            hintFieldValues[i].offset = dim;
        } else if (hintFieldVal.operand == opType::challenge) {
            hintFieldValues[i].size = FIELD_EXTENSION;
            hintFieldValues[i].fieldType = HintFieldType::FieldExtended;
            hintFieldValues[i].offset = FIELD_EXTENSION;
        } else if (hintFieldVal.operand == opType::string_) {
            hintFieldValues[i].string_size = hintFieldVal.stringValue.size();
            hintFieldValues[i].fieldType = HintFieldType::String;
            hintFieldValues[i].offset = 0;
        } else {
            zklog.error("Unknown HintFieldType");
            exitProcess();
            exit(-1);
        }

        hintFieldValues[i].matrix_size = hintFieldVal.pos.size();
    }
    
    return;
}

void getHintField(
    SetupCtx& setupCtx,
    StepsParams &params,
    ExpressionsCtx& expressionsCtx,
    HintFieldInfo *hintFieldValues,
    uint64_t hintId, 
    std::string hintFieldName, 
    HintFieldOptions& hintOptions
) {

    if(setupCtx.expressionsBin.hints.size() == 0) {
        zklog.error("No hints were found.");
        exitProcess();
        exit(-1);
    }

    Hint hint = setupCtx.expressionsBin.hints[hintId];
    
    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldName](const HintField& hintField) {
        return hintField.name == hintFieldName;
    });

    if(hintField == hint.fields.end()) {
        zklog.error("Hint field " + hintFieldName + " not found in hint " + hint.name + ".");
        exitProcess();
        exit(-1);
    }

    for(uint64_t i = 0; i < hintField->values.size(); ++i) {
        HintFieldValue hintFieldVal = hintField->values[i];
        if(hintOptions.dest && (hintFieldVal.operand != opType::cm && hintFieldVal.operand != opType::airgroupvalue && hintFieldVal.operand != opType::airvalue)) {
            zklog.error("Invalid destination.");
            exitProcess();
            exit(-1);
        }

        HintFieldInfo hintFieldInfo = hintFieldValues[i];

        if(hintOptions.print_expression) {
            std::string expression_line = getExpressionDebug(setupCtx, hintId, hintFieldName, hintFieldVal);
            std::memcpy(hintFieldInfo.expression_line, expression_line.data(), expression_line.size());
            hintFieldInfo.expression_line_size = expression_line.size();
        }
        if(hintFieldVal.operand == opType::cm) {
            if(!hintOptions.dest) {
                std::string stage = "cm" + to_string(setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].stage);
                uint64_t offset = setupCtx.starkInfo.mapOffsets[std::make_pair(stage, false)];
                Goldilocks::Element *pAddress = setupCtx.starkInfo.cmPolsMap[hintFieldVal.id].stage == 1 ? params.trace : &params.aux_trace[offset];
                getPolynomial(setupCtx, pAddress, hintFieldInfo.values, setupCtx.starkInfo.cmPolsMap[hintFieldVal.id], hintFieldVal.rowOffsetIndex, "cm");
                if(hintOptions.inverse) {
                    zklog.error("Inverse not supported still for polynomials");
                    exitProcess();
                }
            } else if(hintOptions.initialize_zeros) {
                memset((uint8_t *)hintFieldInfo.values, 0, hintFieldInfo.size * sizeof(Goldilocks::Element));
            }
        } else if(hintFieldVal.operand == opType::custom) {
            getPolynomial(setupCtx, &params.pCustomCommitsFixed[setupCtx.starkInfo.mapOffsets[std::make_pair(setupCtx.starkInfo.customCommits[hintFieldVal.commitId].name + "0", false)]], hintFieldInfo.values, setupCtx.starkInfo.customCommitsMap[hintFieldVal.commitId][hintFieldVal.id], hintFieldVal.rowOffsetIndex, "custom");
            if(hintOptions.inverse) {
                zklog.error("Inverse not supported still for polynomials");
                exitProcess();
            }
        } else if(hintFieldVal.operand == opType::const_) {
            getPolynomial(setupCtx, params.pConstPolsAddress, hintFieldInfo.values, setupCtx.starkInfo.constPolsMap[hintFieldVal.id], hintFieldVal.rowOffsetIndex, "const");
            if(hintOptions.inverse) {
                zklog.error("Inverse not supported still for polynomials");
                exitProcess();
            }
        } else if (hintFieldVal.operand == opType::tmp) {
                ProverHelpers proverHelpers;

            if(hintOptions.compilation_time) {
                ExpressionsPack expressionsCtx(setupCtx, &proverHelpers, 1);
                expressionsCtx.calculateExpression(params, hintFieldInfo.values, hintFieldVal.id, hintOptions.inverse, true);
            } else {
                expressionsCtx.calculateExpression(params, hintFieldInfo.values, hintFieldVal.id, hintOptions.inverse, false);
            }
        } else if (hintFieldVal.operand == opType::public_) {
            hintFieldInfo.values[0] = hintOptions.inverse ? Goldilocks::inv(params.publicInputs[hintFieldVal.id]) : params.publicInputs[hintFieldVal.id];
        } else if (hintFieldVal.operand == opType::number) {
            hintFieldInfo.values[0] = hintOptions.inverse ? Goldilocks::inv(Goldilocks::fromU64(hintFieldVal.value)) : Goldilocks::fromU64(hintFieldVal.value);
        } else if (hintFieldVal.operand == opType::airgroupvalue) {
            if(!hintOptions.dest) {
                uint64_t pos = 0;
                for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
                    pos += setupCtx.starkInfo.airgroupValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
                }
                if(hintOptions.inverse)  {
                    Goldilocks3::inv((Goldilocks3::Element *)hintFieldInfo.values, (Goldilocks3::Element *)&params.airgroupValues[pos]);
                } else {
                    std::memcpy(hintFieldInfo.values, &params.airgroupValues[pos], hintFieldInfo.size * sizeof(Goldilocks::Element));
                }
            }
        } else if (hintFieldVal.operand == opType::airvalue) {
            if(!hintOptions.dest) {
                uint64_t pos = 0;
                for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
                    pos += setupCtx.starkInfo.airValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
                }
                if(hintOptions.inverse)  {
                    Goldilocks3::inv((Goldilocks3::Element *)hintFieldInfo.values, (Goldilocks3::Element *)&params.airValues[pos]);
                } else {
                    std::memcpy(hintFieldInfo.values, &params.airValues[pos], hintFieldInfo.size * sizeof(Goldilocks::Element));
                }
            }
        } else if (hintFieldVal.operand == opType::proofvalue) {
            if(!hintOptions.dest) {
                uint64_t pos = 0;
                for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
                    pos += setupCtx.starkInfo.proofValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
                }
                if(hintOptions.inverse)  {
                    Goldilocks3::inv((Goldilocks3::Element *)hintFieldInfo.values, (Goldilocks3::Element *)&params.proofValues[pos]);
                } else {
                    std::memcpy(hintFieldInfo.values, &params.proofValues[pos], hintFieldInfo.size * sizeof(Goldilocks::Element));
                }
            }
        } else if (hintFieldVal.operand == opType::challenge) {
            if(hintOptions.inverse) {
                Goldilocks3::inv((Goldilocks3::Element *)hintFieldInfo.values, (Goldilocks3::Element *)&params.challenges[FIELD_EXTENSION*hintFieldVal.id]);
            } else {
                std::memcpy(hintFieldInfo.values, &params.challenges[FIELD_EXTENSION*hintFieldVal.id], hintFieldInfo.size * sizeof(Goldilocks::Element));
            }
        } else if (hintFieldVal.operand == opType::string_) {
            std::memcpy(hintFieldInfo.stringValue, hintFieldVal.stringValue.data(), hintFieldVal.stringValue.size()); 
        } else {
            zklog.error("Unknown HintFieldType");
            exitProcess();
            exit(-1);
        }

        for(uint64_t i = 0; i < hintFieldInfo.matrix_size; ++i) {
            hintFieldInfo.pos[i] =  hintFieldVal.pos[i];
        }
    }
    
    return;
}

uint64_t setHintField(SetupCtx& setupCtx, StepsParams& params, Goldilocks::Element* values, uint64_t hintId, std::string hintFieldName) {
    Hint hint = setupCtx.expressionsBin.hints[hintId];

    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldName](const HintField& hintField) {
        return hintField.name == hintFieldName;
    });

    if(hintField == hint.fields.end()) {
        zklog.error("Hint field " + hintFieldName + " not found in hint " + hint.name + ".");
        exitProcess();
        exit(-1);
    }

    if(hintField->values.size() != 1) {
        zklog.error("Hint field " + hintFieldName + " in " + hint.name + "has more than one destination.");
        exitProcess();
        exit(-1);
    }

    auto hintFieldVal = hintField->values[0];
    if(hintFieldVal.operand == opType::cm) {
        setPolynomial(setupCtx, params.aux_trace, values, hintFieldVal.id);
    } else if(hintFieldVal.operand == opType::airgroupvalue) {
        uint64_t pos = 0;
        for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
            pos += setupCtx.starkInfo.airgroupValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
        }
        uint64_t dim = setupCtx.starkInfo.airgroupValuesMap[hintFieldVal.id].stage == 1 ? 1 : FIELD_EXTENSION;
        std::memcpy(&params.airgroupValues[pos], values, dim * sizeof(Goldilocks::Element));
    } else if(hintFieldVal.operand == opType::airvalue) {
        uint64_t pos = 0;
        for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
            pos += setupCtx.starkInfo.airValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
        }
        uint64_t dim = setupCtx.starkInfo.airValuesMap[hintFieldVal.id].stage == 1 ? 1 : FIELD_EXTENSION;
        cout << "hintFieldVal.id = " << hintFieldVal.id << " pos = " << pos << " dim = " << dim << endl;
        std::memcpy(&params.airValues[pos], values, dim * sizeof(Goldilocks::Element));
    } else {
        zklog.error("Only committed pols and airgroupvalues can be set");
        exitProcess();
        exit(-1);  
    }

    return hintFieldVal.id;
}

void addHintField(SetupCtx& setupCtx, StepsParams& params, uint64_t hintId, Dest &destStruct, std::string hintFieldName, HintFieldOptions hintFieldOptions) {
    Hint hint = setupCtx.expressionsBin.hints[hintId];
    
    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldName](const HintField& hintField) {
        return hintField.name == hintFieldName;
    });
    HintFieldValue hintFieldVal = hintField->values[0];

    if(hintField == hint.fields.end()) {
        zklog.error("Hint field " + hintFieldName + " not found in hint " + hint.name + ".");
        exitProcess();
        exit(-1);
    }

    if(hintFieldOptions.print_expression) {
        std::string expression_line = getExpressionDebug(setupCtx, hintId, hintFieldName, hintFieldVal);
    }
    if(hintFieldVal.operand == opType::cm) {
        destStruct.addCmPol(setupCtx.starkInfo.cmPolsMap[hintFieldVal.id], hintFieldVal.rowOffsetIndex, hintFieldOptions.inverse);
    } else if(hintFieldVal.operand == opType::const_) {
        destStruct.addConstPol(setupCtx.starkInfo.constPolsMap[hintFieldVal.id], hintFieldVal.rowOffsetIndex, hintFieldOptions.inverse);
    } else if(hintFieldVal.operand == opType::number) {
        if(hintFieldVal.value == 1 && destStruct.params.size() > 0) {
            return;
        } 
        destStruct.addNumber(hintFieldVal.value, hintFieldOptions.inverse);
    } else if(hintFieldVal.operand == opType::tmp) {
        destStruct.addParams(hintFieldVal.id, setupCtx.expressionsBin.expressionsInfo[hintFieldVal.id].destDim, hintFieldOptions.inverse);
    } else if(hintFieldVal.operand == opType::airvalue) {
        uint64_t airValuePos = 0;
        for(uint64_t i = 0; i < hintFieldVal.id; ++i) {
            if(setupCtx.starkInfo.airValuesMap[i].stage == 1) {
                airValuePos += 1;
            } else {
                airValuePos += FIELD_EXTENSION;
            }
        }
        destStruct.addAirValue(setupCtx.starkInfo.airValuesMap[hintFieldVal.id].stage, airValuePos, hintFieldOptions.inverse);   
    } else {
        zklog.error("Op type " + to_string(hintFieldVal.operand) + "is not considered yet.");
        exitProcess();
        exit(-1);
    }
}

void calculateExpr(SetupCtx& setupCtx, StepsParams &params, ExpressionsCtx& expressionsCtx, uint64_t nHints, uint64_t* hintId, std::string *hintFieldNameDest, std::string* hintFieldName,  HintFieldOptions *hintOptions) {
    if(setupCtx.expressionsBin.hints.size() == 0) {
        zklog.error("No hints were found.");
        exitProcess();
        exit(-1);
    }

    std::vector<Dest> dests;
    Goldilocks::Element *buff = NULL;

    for(uint64_t i = 0; i < nHints; ++i) {
        Hint hint = setupCtx.expressionsBin.hints[hintId[i]];
        std::string hintDest = hintFieldNameDest[i];
        auto hintFieldDest = std::find_if(hint.fields.begin(), hint.fields.end(), [hintDest](const HintField& hintField) {
            return hintField.name == hintDest;
        });
        HintFieldValue hintFieldDestVal = hintFieldDest->values[0];

        uint64_t offset = 0;
        uint64_t nRows;
        if(hintFieldDestVal.operand == opType::cm) {
            offset = setupCtx.starkInfo.mapSectionsN["cm" + to_string(setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].stage)];
            buff = &params.trace[setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].stagePos];
            nRows = 1 << setupCtx.starkInfo.starkStruct.nBits;
        } else if (hintFieldDestVal.operand == opType::airvalue) {
            nRows = 1;
            uint64_t pos = 0;
            for(uint64_t i = 0; i < hintFieldDestVal.id; ++i) {
                pos += setupCtx.starkInfo.airValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
            }
            buff = &params.airValues[pos];
        } else {
            zklog.error("Only committed pols and airvalues can be set");
            exitProcess();
            exit(-1);
        }

        Dest destStruct(buff, nRows, offset);

        addHintField(setupCtx, params, hintId[i], destStruct, hintFieldName[i], hintOptions[i]);
        expressionsCtx.calculateExpressions(params, destStruct, nRows, false, false);
    }
}

void multiplyHintFields(SetupCtx& setupCtx, StepsParams &params, ExpressionsCtx& expressionsCtx, uint64_t nHints, uint64_t* hintId, std::string *hintFieldNameDest, std::string* hintFieldName1, std::string* hintFieldName2,  HintFieldOptions *hintOptions1, HintFieldOptions *hintOptions2) {
    if(setupCtx.expressionsBin.hints.size() == 0) {
        zklog.error("No hints were found.");
        exitProcess();
        exit(-1);
    }

    std::vector<Dest> dests;
    Goldilocks::Element *buff = NULL;

    for(uint64_t i = 0; i < nHints; ++i) {
        Hint hint = setupCtx.expressionsBin.hints[hintId[i]];
        std::string hintDest = hintFieldNameDest[i];
        auto hintFieldDest = std::find_if(hint.fields.begin(), hint.fields.end(), [hintDest](const HintField& hintField) {
            return hintField.name == hintDest;
        });
        HintFieldValue hintFieldDestVal = hintFieldDest->values[0];

        uint64_t offset = 0;
        uint64_t nRows;
        if(hintFieldDestVal.operand == opType::cm) {
            offset = setupCtx.starkInfo.mapSectionsN["cm" + to_string(setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].stage)];
            uint64_t offsetAuxTrace = setupCtx.starkInfo.mapOffsets[std::make_pair("cm" + to_string(setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].stage), false)] + setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].stagePos;           
            buff = &params.aux_trace[offsetAuxTrace];           
            nRows = 1 << setupCtx.starkInfo.starkStruct.nBits;
        } else if (hintFieldDestVal.operand == opType::airvalue) {
            nRows = 1;
            uint64_t pos = 0;
            for(uint64_t i = 0; i < hintFieldDestVal.id; ++i) {
                pos += setupCtx.starkInfo.airValuesMap[i].stage == 1 ? 1 : FIELD_EXTENSION;
            }
            buff = &params.airValues[pos];
        } else {
            zklog.error("Only committed pols and airvalues can be set");
            exitProcess();
            exit(-1);
        }

        Dest destStruct(buff, nRows, offset);

        addHintField(setupCtx, params, hintId[i], destStruct, hintFieldName1[i], hintOptions1[i]);
        addHintField(setupCtx, params, hintId[i], destStruct, hintFieldName2[i], hintOptions2[i]);
        expressionsCtx.calculateExpressions(params, destStruct, nRows, false, false);
    }
}

void accHintField(SetupCtx& setupCtx, StepsParams &params, ExpressionsCtx &expressionsCtx, uint64_t hintId, std::string hintFieldNameDest, std::string hintFieldNameAirgroupVal, std::string hintFieldName, bool add) {
    uint64_t N = 1 << setupCtx.starkInfo.starkStruct.nBits;

    Hint hint = setupCtx.expressionsBin.hints[hintId];

    auto hintFieldDest = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldNameDest](const HintField& hintField) {
        return hintField.name == hintFieldNameDest;
    });

    HintFieldOptions hintOptions;
    HintFieldValue hintFieldDestVal = hintFieldDest->values[0];

    uint64_t dim = setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].dim;
    
    Goldilocks::Element *vals = setupCtx.starkInfo.verify_constraints
        ? new Goldilocks::Element[dim*N]
        : &params.aux_trace[setupCtx.starkInfo.mapOffsets[std::make_pair("q", true)]];
    
    Dest destStruct(vals, N, 0);
    addHintField(setupCtx, params, hintId, destStruct, hintFieldName, hintOptions);

    expressionsCtx.calculateExpressions(params, destStruct, N, false, false);

    // Parallel prefix scan replaces the serial `vals[i] OP= vals[i-1]` chain.
    prefix_scan_inplace(vals, N, /*stride=*/(uint64_t)dim, /*is_add=*/add);

    setHintField(setupCtx, params, vals, hintId, hintFieldNameDest);
    setHintField(setupCtx, params, &vals[(N - 1)*FIELD_EXTENSION], hintId, hintFieldNameAirgroupVal);

    if(setupCtx.starkInfo.verify_constraints) {
        delete[] vals;
    }
}

uint64_t getHintId(SetupCtx& setupCtx, uint64_t hintId, std::string name) {
    Hint hint = setupCtx.expressionsBin.hints[hintId];

    auto hintField = std::find_if(hint.fields.begin(), hint.fields.end(), [name](const HintField& hintField) {
        return hintField.name == name;
    });
    return hintField->values[0].id;
}

void accMulHintFields(SetupCtx& setupCtx, StepsParams &params, ExpressionsCtx &expressionsCtx, uint64_t hintId, std::string hintFieldNameDest, std::string hintFieldNameAirgroupVal, std::string hintFieldName1, std::string hintFieldName2, HintFieldOptions &hintOptions1, HintFieldOptions &hintOptions2, bool add) {
    
    uint64_t N = 1 << setupCtx.starkInfo.starkStruct.nBits;
    Hint hint = setupCtx.expressionsBin.hints[hintId];

    auto hintFieldDest = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldNameDest](const HintField& hintField) {
        return hintField.name == hintFieldNameDest;
    });
    HintFieldValue hintFieldDestVal = hintFieldDest->values[0];

    uint64_t dim = setupCtx.starkInfo.cmPolsMap[hintFieldDestVal.id].dim;
    
    uint64_t offsetAuxTrace = setupCtx.starkInfo.mapOffsets[std::make_pair("q", true)];
    Goldilocks::Element *vals = setupCtx.starkInfo.verify_constraints
    ? new Goldilocks::Element[dim*N]
    : &params.aux_trace[offsetAuxTrace];

    Dest destStruct(vals, 1 << setupCtx.starkInfo.starkStruct.nBits, 0);
    addHintField(setupCtx, params, hintId, destStruct, hintFieldName1, hintOptions1);
    addHintField(setupCtx, params, hintId, destStruct, hintFieldName2, hintOptions2);
    expressionsCtx.calculateExpressions(params, destStruct, N, false, false);
    // Parallel prefix scan replaces the serial `vals[i] OP= vals[i-1]` chain.
    prefix_scan_inplace(vals, N, /*stride=*/(uint64_t)dim, /*is_add=*/add);

    setHintField(setupCtx, params, vals, hintId, hintFieldNameDest);
    if (hintFieldNameAirgroupVal != "") {
        setHintField(setupCtx, params, &vals[(N - 1)*FIELD_EXTENSION], hintId, hintFieldNameAirgroupVal);
    }

    if(setupCtx.starkInfo.verify_constraints) {
        delete[] vals;
    }
}

uint64_t updateAirgroupValue(SetupCtx& setupCtx, StepsParams &params, uint64_t hintId, std::string hintFieldNameAirgroupVal, std::string hintFieldName1, std::string hintFieldName2, HintFieldOptions &hintOptions1, HintFieldOptions &hintOptions2, bool add) {
    
    if (hintFieldNameAirgroupVal == "") return 0;

    Hint hint = setupCtx.expressionsBin.hints[hintId];

    auto hintFieldAirgroup = std::find_if(hint.fields.begin(), hint.fields.end(), [hintFieldNameAirgroupVal](const HintField& hintField) {
        return hintField.name == hintFieldNameAirgroupVal;
    });
    HintFieldValue hintFieldAirgroupVal = hintFieldAirgroup->values[0];

    Goldilocks::Element vals[3];
    
    Dest destStruct(vals, 1, 0);
    addHintField(setupCtx, params, hintId, destStruct, hintFieldName1, hintOptions1);
    addHintField(setupCtx, params, hintId, destStruct, hintFieldName2, hintOptions2);

    ProverHelpers proverHelpers;
    ExpressionsPack expressionsCtx(setupCtx, &proverHelpers, 1);
    expressionsCtx.calculateExpressions(params, destStruct, 1, false, false);

    Goldilocks::Element *airgroupValue = &params.airgroupValues[FIELD_EXTENSION*hintFieldAirgroupVal.id];
    if(add) {
        if(destStruct.dim == 1) {
            Goldilocks::add(airgroupValue[0], airgroupValue[0], vals[0]);
        } else {
            Goldilocks3::add((Goldilocks3::Element &)airgroupValue[0], (Goldilocks3::Element &)airgroupValue[0], (Goldilocks3::Element &)vals[0]);
        }
    } else {
        if(destStruct.dim == 1) {
            Goldilocks::mul(airgroupValue[0], airgroupValue[0], vals[0]);
        } else {
            Goldilocks3::mul((Goldilocks3::Element &)airgroupValue[0], (Goldilocks3::Element &)airgroupValue[0], (Goldilocks3::Element &)vals[0]);
        }
    }

    return hintFieldAirgroupVal.id;
}
