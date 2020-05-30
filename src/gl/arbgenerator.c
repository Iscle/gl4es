#include "arbgenerator.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "arbhelper.h"
#include "state.h"

#define FAIL(str) curStatusPtr->status = ST_ERROR; if (glsl->error_msg) free(glsl->error_msg); \
		glsl->error_msg = strdup(str); return
void generateVariablePre(sCurStatus *curStatusPtr, int vertex, glsl_t *glsl, sVariable *varPtr) {
	if (varPtr->type == VARTYPE_CONST) {
		return;
	}
	
	APPEND_OUTPUT("\tvec4 ", 6)
	APPEND_OUTPUT2(varPtr->names[0])
	
	switch (varPtr->type) {
	case VARTYPE_ADDRESS:
	case VARTYPE_ATTRIB:
	case VARTYPE_OUTPUT:
	case VARTYPE_PARAM:
		APPEND_OUTPUT(" = ", 3)
		APPEND_OUTPUT(varPtr->init.strings[0], varPtr->init.strings_total_len)
		break;
		
	case VARTYPE_PARAM_MULT:
		APPEND_OUTPUT("[", 1)
		if (varPtr->size > 0) {
			char buf[11]; /* Assume 32-bits array address, should never overflow... */
			sprintf(buf, "%d", varPtr->size);
			APPEND_OUTPUT2(buf)
		}
		APPEND_OUTPUT("]", 1)
	
		APPEND_OUTPUT(" = ", 3)
		APPEND_OUTPUT("vec4[](", 7)
		for (size_t i = 0; i < varPtr->init.strings_count; ++i) {
			APPEND_OUTPUT2(varPtr->init.strings[i])
			APPEND_OUTPUT(", ", 2)
		}
		--curStatusPtr->outputEnd;
		--curStatusPtr->outLen;
		++curStatusPtr->outLeft;
		curStatusPtr->outputEnd[-1] = ')';
		break;
		
	case VARTYPE_CONST:
	case VARTYPE_TEMP:
		break;
		
	case VARTYPE_ALIAS:
	case VARTYPE_TEXTURE:
	case VARTYPE_TEXTARGET:
	case VARTYPE_UNK:
		FAIL("Invalid variable type (unintended fallthrough?)");
	}
	
	APPEND_OUTPUT(";\n", 2)
}
void generateInstruction(sCurStatus *curStatusPtr, int vertex, glsl_t *glsl, sInstruction *instPtr) {
// Data access and output
#define SWIZ(i, s) instPtr->vars[i].swizzle[s]
#define PUSH_SWIZZLE(s) \
		switch (s) {               \
		case SWIZ_X:               \
			APPEND_OUTPUT("x", 1); \
			break;                 \
		case SWIZ_Y:               \
			APPEND_OUTPUT("y", 1); \
			break;                 \
		case SWIZ_Z:               \
			APPEND_OUTPUT("z", 1); \
			break;                 \
		case SWIZ_W:               \
			APPEND_OUTPUT("w", 1); \
			break;                 \
		case SWIZ_NONE:            \
			break;                 \
		}
	
	// Instruction assertions
#define ASSERT_COUNT(cnt) \
		if (((cnt < MAX_OPERANDS) && instPtr->vars[cnt].var) || (cnt && !instPtr->vars[cnt - 1].var)) { \
			FAIL("Invalid instruction (not enough/too many arguments)");                                \
		}
#define ASSERT_MASKDST(i) \
		if ((instPtr->vars[i].var->type != VARTYPE_TEMP) && (instPtr->vars[i].var->type != VARTYPE_OUTPUT) \
		 && (instPtr->vars[i].var->type != VARTYPE_CONST)) {                                               \
			FAIL("Variable is not a valid masked destination register");                                   \
		}                                                                                                  \
		if (instPtr->vars[i].sign != 0) {                                                                  \
			FAIL("Variable is not a valid masked destination register");                                   \
		}                                                                                                  \
		if (instPtr->vars[i].floatArrAddr != -1) {                                                         \
			FAIL("Variable is not a valid masked destination register");                                   \
		}                                                                                                  \
		for (int sw = 0; (sw < 3) && (SWIZ(i, sw + 1) != SWIZ_NONE); ++sw) {                               \
			if ((SWIZ(i, sw) >= SWIZ(i, sw + 1))) {                                                        \
				FAIL("Variable is not a valid masked destination register");                               \
			}                                                                                              \
		}                                                                                                  \
		if (curStatusPtr->status == ST_ERROR) {                                                            \
			return;                                                                                      \
		}
#define ASSERT_VECTSRC(i) \
		if ((instPtr->vars[i].var->type != VARTYPE_TEMP) && (instPtr->vars[i].var->type != VARTYPE_ATTRIB) \
		 && (instPtr->vars[i].var->type != VARTYPE_PARAM) && (instPtr->vars[i].var->type != VARTYPE_CONST) \
		 && (instPtr->vars[i].var->type != VARTYPE_PARAM_MULT)) {                                          \
			FAIL("Variable is not a valid vector source register");                                        \
		}                                                                                                  \
		if ((SWIZ(i, 1) != SWIZ_NONE) && (SWIZ(i, 3) == SWIZ_NONE)) {                                      \
			FAIL("Variable is not a valid vector source register");                                        \
		}
#define ASSERT_SCALSRC(i) \
		if ((instPtr->vars[i].var->type != VARTYPE_TEMP) && (instPtr->vars[i].var->type != VARTYPE_ATTRIB) \
		 && (instPtr->vars[i].var->type != VARTYPE_PARAM) && (instPtr->vars[i].var->type != VARTYPE_CONST) \
		 && (instPtr->vars[i].var->type != VARTYPE_PARAM_MULT)) {                                          \
			FAIL("Variable is not a valid vector source scalar");                                          \
		}                                                                                                  \
		if ((SWIZ(i, 0) == SWIZ_NONE) || (SWIZ(i, 1) != SWIZ_NONE)) {                                      \
			FAIL("Variable is not a valid vector source scalar");                                          \
		}
#define INST_VECTOR \
		ASSERT_COUNT(2)   \
		ASSERT_MASKDST(0) \
		ASSERT_VECTSRC(1)
#define INST_SCALAR \
		ASSERT_COUNT(2)   \
		ASSERT_MASKDST(0) \
		ASSERT_SCALSRC(1)
#define INST_BINSCL \
		ASSERT_COUNT(3)   \
		ASSERT_MASKDST(0) \
		ASSERT_SCALSRC(1) \
		ASSERT_SCALSRC(2)
#define INST_BINVEC \
		ASSERT_COUNT(3)   \
		ASSERT_MASKDST(0) \
		ASSERT_VECTSRC(1) \
		ASSERT_VECTSRC(2)
#define INST_TRIVEC \
		ASSERT_COUNT(4)   \
		ASSERT_MASKDST(0) \
		ASSERT_VECTSRC(1) \
		ASSERT_VECTSRC(2) \
		ASSERT_VECTSRC(3)
#define INST_SAMPLE \
		ASSERT_COUNT(4)                                                                                      \
		ASSERT_MASKDST(0)                                                                                    \
		ASSERT_VECTSRC(1)                                                                                    \
		if (instPtr->vars[2].var->type != VARTYPE_TEXTURE) {                                                 \
			FAIL("Invalid texture variable");                                                                \
		}                                                                                                    \
		if ((instPtr->vars[3].var != curStatusPtr->tex1D) && (instPtr->vars[3].var != curStatusPtr->tex2D)   \
		 && (instPtr->vars[3].var != curStatusPtr->tex3D) && (instPtr->vars[3].var != curStatusPtr->texCUBE) \
		 /* && (instPtr->vars[3].var != curStatusPtr->texRECT) */) {                                         \
			FAIL("Invalid texture sampler target");                                                          \
		}
	
	// Misc pushing
#define PUSH_DSTMASK(i) \
		if (SWIZ(i, 0) != SWIZ_NONE) {                                       \
			APPEND_OUTPUT(".", 1)                                            \
			for (int sw = 0; (sw < 4) && (SWIZ(i, sw) != SWIZ_NONE); ++sw) { \
				PUSH_SWIZZLE(SWIZ(i, sw))                                    \
			}                                                                \
		}
#define PUSH_VARNAME(i) \
		if (instPtr->vars[i].sign == -1) {                        \
			APPEND_OUTPUT("-", 1)                                 \
		}                                                         \
		if (instPtr->vars[i].var->type == VARTYPE_CONST) {        \
			APPEND_OUTPUT2(instPtr->vars[i].var->init.strings[0]) \
		} else {                                                  \
			APPEND_OUTPUT2(instPtr->vars[i].var->names[0])        \
		}                                                         \
		if (instPtr->vars[i].floatArrAddr != -1) {                \
			char buf[11];                                         \
			sprintf(buf, "%d", instPtr->vars[i].floatArrAddr);    \
			APPEND_OUTPUT("[", 1)                                 \
			APPEND_OUTPUT2(buf)                                   \
			APPEND_OUTPUT("]", 1)                                 \
		}
#define PUSH_PRE_SAT(p) \
		if (instPtr->saturated) {      \
			APPEND_OUTPUT("clamp(", 6) \
		} else if (p) {                \
			APPEND_OUTPUT("(", 1)      \
		}
#define PUSH_POSTSAT(p) \
		if (instPtr->saturated) {         \
			APPEND_OUTPUT(", 0., 1.)", 9) \
		} else if (p) {                   \
			APPEND_OUTPUT(")", 1)         \
		}
	
	// Instruction variable pushing
	// TODO: MOV, LG2 and similar use only (a) specific component(s) (mask), optimize generated code
#define PUSH_MASKDST(i) \
		PUSH_VARNAME(i) \
		PUSH_DSTMASK(i)
#define PUSH_VECTSRC(i) \
		PUSH_VARNAME(i)                    \
		if (SWIZ(i, 0) != SWIZ_NONE) {     \
			APPEND_OUTPUT(".", 1)          \
			PUSH_SWIZZLE(SWIZ(i, 0))       \
			if (SWIZ(i, 3) == SWIZ_NONE) { \
				PUSH_SWIZZLE(SWIZ(i, 0))   \
				PUSH_SWIZZLE(SWIZ(i, 0))   \
				PUSH_SWIZZLE(SWIZ(i, 0))   \
			} else {                       \
				PUSH_SWIZZLE(SWIZ(i, 1))   \
				PUSH_SWIZZLE(SWIZ(i, 2))   \
				PUSH_SWIZZLE(SWIZ(i, 3))   \
			}                              \
		}
/* Append a VEctor SouRCe ComponenT */
#define PUSH_VESRCCT(i, s) \
		PUSH_VARNAME(i)                    \
		APPEND_OUTPUT(".", 1)              \
		if (SWIZ(i, 0) == SWIZ_NONE) {     \
			PUSH_SWIZZLE(s + 1)            \
		} else {                           \
			if (SWIZ(i, 3) == SWIZ_NONE) { \
				PUSH_SWIZZLE(SWIZ(i, 0))   \
			} else {                       \
				PUSH_SWIZZLE(SWIZ(i, s))   \
			}                              \
		}
#define PUSH_SCALSRC(i) \
		PUSH_VARNAME(i)          \
		APPEND_OUTPUT(".", 1)    \
		PUSH_SWIZZLE(SWIZ(i, 0)) \
		PUSH_SWIZZLE(SWIZ(i, 0)) \
		PUSH_SWIZZLE(SWIZ(i, 0)) \
		PUSH_SWIZZLE(SWIZ(i, 0))
	
	// Textures
/* Append a VECTor SaMPler */
#define PUSH_VECTSMP(i, j) \
		PUSH_VECTSRC(i)                                             \
		if (instPtr->vars[j].var == curStatusPtr->tex1D) {          \
			APPEND_OUTPUT(".x", 2)                                  \
		} else if (instPtr->vars[j].var == curStatusPtr->tex2D) {   \
			APPEND_OUTPUT(".xy", 3)                                 \
		} else if (instPtr->vars[j].var == curStatusPtr->tex3D) {   \
			APPEND_OUTPUT(".xyz", 4)                                \
		} else if (instPtr->vars[j].var == curStatusPtr->texCUBE) { \
			APPEND_OUTPUT(".xyz", 4)                                \
		} else {                                                    \
			FAIL("Invalid variable texture target");                \
		}
/* Append a texture SAMPLER */
#define PUSH_SAMPLER(i, j) \
		if (instPtr->vars[i].var->type != VARTYPE_TEXTURE) {        \
			FAIL("Invalid variable type");                          \
		}                                                           \
		APPEND_OUTPUT("samplers", 8)                                \
		if (instPtr->vars[j].var == curStatusPtr->tex1D) {          \
			APPEND_OUTPUT("1D", 2)                                  \
		} else if (instPtr->vars[j].var == curStatusPtr->tex2D) {   \
			APPEND_OUTPUT("2D", 2)                                  \
		} else if (instPtr->vars[j].var == curStatusPtr->tex3D) {   \
			APPEND_OUTPUT("3D", 2)                                  \
		} else if (instPtr->vars[j].var == curStatusPtr->texCUBE) { \
			APPEND_OUTPUT("Cube", 4)                                \
		} else {                                                    \
			FAIL("Invalid variable texture target");                \
		}                                                           \
		APPEND_OUTPUT("[", 1)                                       \
		APPEND_OUTPUT2(instPtr->vars[i].var->names[0])              \
		APPEND_OUTPUT("]", 1)
/* Append a SAMPler FunCtioN */
#define PUSH_SAMPFCN(i) \
		if (instPtr->vars[i].var == curStatusPtr->tex1D) {          \
			APPEND_OUTPUT("1D", 2)                                  \
		} else if (instPtr->vars[i].var == curStatusPtr->tex2D) {   \
			APPEND_OUTPUT("2D", 2)                                  \
		} else if (instPtr->vars[i].var == curStatusPtr->tex3D) {   \
			APPEND_OUTPUT("3D", 2)                                  \
		} else if (instPtr->vars[i].var == curStatusPtr->texCUBE) { \
			APPEND_OUTPUT("Cube", 4)                                \
		} else {                                                    \
			FAIL("Invalid variable texture target");                \
		}
	
	// Misc
#define FINISH_INST \
		PUSH_DSTMASK(0)         \
		APPEND_OUTPUT(";\n", 2) \
		break;
	
	switch (instPtr->type) {
	case INST_ABS:
		INST_VECTOR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("abs(", 4)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_ADD:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(" + ", 3)
		PUSH_VECTSRC(2)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_ARL:
		// TODO
		FAIL("ARBconv TODO: ARL");
		break;
		
		/* Old version
		if (!vertex) {
			FAIL("Invalid instruction in fragment shaders");
		}
		ASSERT_COUNT(2)
		APPEND_OUTPUT("\t", 1)
		PUSH_SCAL(0)
		APPEND_OUTPUT(" = floor(", 9)
		if (instPtr->vars[1].var->type != VARTYPE_ADDRESS) {
			FAIL("Variable is not an address");
		}
		if ((SWIZ(1, 0) != SWIZ_X) || (SWIZ(1, 1) != SWIZ_NONE)) {
			FAIL("Variable is not an address");
		}
		if (instPtr->vars[1].floatArrAddr != -1) {
			FAIL("Variable is not an address");
		}
		APPEND_OUTPUT2(instPtr->vars[1].var->names[0])
		APPEND_OUTPUT(".x", 2)
		APPEND_OUTPUT(")", 1)
		FINISH_INST
		break; */
		
	case INST_CMP:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_TRIVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4((", 6)
		PUSH_VESRCCT(1, 0)
		APPEND_OUTPUT(" < 0.) ? ", 9)
		PUSH_VESRCCT(2, 0)
		APPEND_OUTPUT(" : ", 3)
		PUSH_VESRCCT(3, 0)
		APPEND_OUTPUT(", (", 3)
		PUSH_VESRCCT(1, 1)
		APPEND_OUTPUT(" < 0.) ? ", 9)
		PUSH_VESRCCT(2, 1)
		APPEND_OUTPUT(" : ", 3)
		PUSH_VESRCCT(3, 1)
		APPEND_OUTPUT(", (", 3)
		PUSH_VESRCCT(1, 2)
		APPEND_OUTPUT(" < 0.) ? ", 9)
		PUSH_VESRCCT(2, 2)
		APPEND_OUTPUT(" : ", 3)
		PUSH_VESRCCT(3, 2)
		APPEND_OUTPUT(", (", 3)
		PUSH_VESRCCT(1, 3)
		APPEND_OUTPUT(" < 0.) ? ", 9)
		PUSH_VESRCCT(2, 3)
		APPEND_OUTPUT(" : ", 3)
		PUSH_VESRCCT(3, 3)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_COS:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(cos(", 9)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("))", 2)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_DP3:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(dot(", 9)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(".xyz, ", 6)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(".xyz))", 6)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_DP4:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(dot(", 9)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT("))", 2)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_DPH:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(dot(", 9)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(".xyz, ", 6)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT("))", 2)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_DST:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(1., ", 9)
		PUSH_VESRCCT(1, 1)
		APPEND_OUTPUT(" * ", 3)
		PUSH_VESRCCT(2, 1)
		APPEND_OUTPUT(", ", 2)
		PUSH_VESRCCT(1, 2)
		APPEND_OUTPUT(", ", 2)
		PUSH_VESRCCT(2, 3)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_EX2: // "Exact"
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		APPEND_OUTPUT("exp2(", 5)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_EXP: // Approximate
		if (!vertex) {
			FAIL("Invalid instruction in fragment shaders");
		}
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = vec4(exp2(floor(", 19)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")), fract(", 10)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("), exp2(", 8)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("), 1.)", 6)
		FINISH_INST
		
	case INST_FLR:
		INST_VECTOR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("floor(", 6)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_FRC:
		INST_VECTOR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("fract(", 6)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_KIL:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		ASSERT_COUNT(1)
		APPEND_OUTPUT("\tif ((", 6)
		PUSH_VESRCCT(0, 0)
		APPEND_OUTPUT(" < 0.) || (", 11)
		PUSH_VESRCCT(0, 1)
		APPEND_OUTPUT(" < 0.) || (", 11)
		PUSH_VESRCCT(0, 2)
		APPEND_OUTPUT(" < 0.) || (", 11)
		PUSH_VESRCCT(0, 3)
		APPEND_OUTPUT(" < 0.)) return;\n", 16);
		break;
		
	case INST_LG2: // "Exact"
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		APPEND_OUTPUT("log2(", 5)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_LIT:
		INST_VECTOR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(1.0, max(", 14)
		PUSH_VESRCCT(1, 0)
		APPEND_OUTPUT(", 0.0), (", 9)
		PUSH_VESRCCT(1, 0)
		APPEND_OUTPUT(" > 0.0) ? pow(max(", 18)
		PUSH_VESRCCT(1, 1)
		APPEND_OUTPUT(", 0.0), clamp(", 14)
		PUSH_VESRCCT(1, 3)
		APPEND_OUTPUT(", -180., 180.)) : 0.0, 1.0)", 27)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_LOG: // Approximate
		if (!vertex) {
			FAIL("Invalid instruction in fragment shaders");
		}
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = vec4(floor(log2(abs(", 23)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("))), abs(", 9)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(") / exp2(floor(log2(abs(", 24)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")))), log2(abs(", 15)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")), 1.)", 7)
		FINISH_INST
		
	case INST_LRP:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_TRIVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("mix(", 4)
		PUSH_VECTSRC(3)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_MAD:
		INST_TRIVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(" * ", 3)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(" + ", 3)
		PUSH_VECTSRC(3)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_MAX:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("max(", 4)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_MIN:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("min(", 4)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_MOV:
		INST_VECTOR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		PUSH_VECTSRC(1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_MUL:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(" * ", 3)
		PUSH_VECTSRC(2)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_POW:
		INST_BINSCL
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("pow(", 4)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(", ", 2)
		PUSH_SCALSRC(2)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_RCP:
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("(1 / ", 5)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_RSQ:
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("(sqrt(1 / ", 10)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("))", 2)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_SCS:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(cos(", 9)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("), sin(", 7)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("), 0., 0.)", 10)
		PUSH_POSTSAT(0)
		FINISH_INST
		break;
		
	case INST_SGE:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = vec4((", 9)
		PUSH_VESRCCT(1, 0)
		APPEND_OUTPUT(" >= ", 4)
		PUSH_VESRCCT(2, 0)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 1)
		APPEND_OUTPUT(" >= ", 4)
		PUSH_VESRCCT(2, 1)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 2)
		APPEND_OUTPUT(" >= ", 4)
		PUSH_VESRCCT(2, 2)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 3)
		APPEND_OUTPUT(" >= ", 4)
		PUSH_VESRCCT(2, 3)
		APPEND_OUTPUT(") ? 1. : 0.)", 12)
		FINISH_INST
		
	case INST_SIN:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SCALAR
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(sin(", 9)
		PUSH_SCALSRC(1)
		APPEND_OUTPUT("))", 2)
		PUSH_POSTSAT(0)
		FINISH_INST
		break;
		
	case INST_SLT:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = vec4((", 9)
		PUSH_VESRCCT(1, 0)
		APPEND_OUTPUT(" < ", 3)
		PUSH_VESRCCT(2, 0)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 1)
		APPEND_OUTPUT(" < ", 3)
		PUSH_VESRCCT(2, 1)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 2)
		APPEND_OUTPUT(" < ", 3)
		PUSH_VESRCCT(2, 2)
		APPEND_OUTPUT(") ? 1. : 0., (", 14)
		PUSH_VESRCCT(1, 3)
		APPEND_OUTPUT(" < ", 3)
		PUSH_VESRCCT(2, 3)
		APPEND_OUTPUT(") ? 1. : 0.)", 12)
		FINISH_INST
		
	case INST_SUB:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(1)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(" - ", 3)
		PUSH_VECTSRC(2)
		PUSH_POSTSAT(1)
		FINISH_INST
		
	case INST_SWZ:
		// TODO
		FAIL("ARBconv TODO: SWZ");
		break;
		
	case INST_TEX:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SAMPLE
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		// APPEND_OUTPUT("texture(", 8)
		APPEND_OUTPUT("texture", 7) // Deprecated! (but texture is not official until 1.30)
		PUSH_SAMPFCN(3)
		APPEND_OUTPUT("(", 1)
		PUSH_SAMPLER(2, 3)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSMP(1, 3)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_TXB:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SAMPLE
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		// APPEND_OUTPUT("texture(", 8)
		APPEND_OUTPUT("texture", 7) // Deprecated! (but texture is not official until 1.30)
		PUSH_SAMPFCN(3)
		APPEND_OUTPUT("(", 1)
		PUSH_SAMPLER(2, 3)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSMP(1, 3)
		APPEND_OUTPUT(", ", 2)
		PUSH_VESRCCT(1, 3)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_TXP:
		if (vertex) {
			FAIL("Invalid instruction in vertex shader");
		}
		INST_SAMPLE
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		// APPEND_OUTPUT("textureProj(", 12)
		APPEND_OUTPUT("texture", 7) // Deprecated! (but texture is not official until 1.30)
		PUSH_SAMPFCN(3)
		APPEND_OUTPUT("Proj(", 5)
		PUSH_SAMPLER(2, 3)
		APPEND_OUTPUT(", ", 2)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(")", 1)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_XPD:
		INST_BINVEC
		APPEND_OUTPUT("\t", 1)
		PUSH_MASKDST(0)
		APPEND_OUTPUT(" = ", 3)
		PUSH_PRE_SAT(0)
		APPEND_OUTPUT("vec4(cross(", 11)
		PUSH_VECTSRC(1)
		APPEND_OUTPUT(".xyz, ", 6)
		PUSH_VECTSRC(2)
		APPEND_OUTPUT(".xyz), 0.)", 10)
		PUSH_POSTSAT(0)
		FINISH_INST
		
	case INST_UNK:
		FAIL("Unknown instruction (unexpected fallthrough?)");
	}
	
#undef FINISH_INST
#undef PUSH_SAMPFCN
#undef PUSH_SAMPLER
#undef PUSH_VECTSMP
#undef PUSH_SCALSRC
#undef PUSH_VESRCCT
#undef PUSH_VECTSRC
#undef PUSH_MASKDST
#undef PUSH_POSTSAT
#undef PUSH_PRE_SAT
#undef PUSH_VARNAME
#undef PUSH_DSTMASK
#undef INST_SAMPLE
#undef INST_TRIVEC
#undef INST_BINVEC
#undef INST_BINSCL
#undef INST_SCALAR
#undef INST_VECTOR
#undef ASSERT_SCALSRC
#undef ASSERT_VECTSRC
#undef ASSERT_MASKDST
#undef ASSERT_COUNT
#undef PUSH_SWIZZLE
#undef SWIZ
}
void generateVariablePst(sCurStatus *curStatusPtr, int vertex, glsl_t *glsl, sVariable *varPtr) {
	if (varPtr->type != VARTYPE_OUTPUT) {
		return;
	}
	
	APPEND_OUTPUT("\t", 1)
	APPEND_OUTPUT(varPtr->init.strings[0], varPtr->init.strings_total_len)
	APPEND_OUTPUT(" = ", 3)
	APPEND_OUTPUT2(varPtr->names[0])
	APPEND_OUTPUT(";\n", 2)
}
