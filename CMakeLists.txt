cmake_minimum_required(VERSION 3.20)
project(krkrvita VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KRKRVITA_BUILD_TESTS "Build host-side tests" ON)
option(KRKRVITA_ENABLE_OPENGL_COMPOSITOR
    "Use Yuri's experimental OpenGL compositor instead of software rendering"
    OFF)

set(KRKRVITA_YURI_TJS_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/yuri/src/core/tjs2")
set(KRKRVITA_YURI_TJS_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/tjs2")
file(MAKE_DIRECTORY "${KRKRVITA_YURI_TJS_GENERATED_DIR}")

# Yuri's layer-tree traversal takes a safe lock for every child list during
# completion.  Its Compact() implementation used to replace the backing array
# even when the list contained no null entries, turning every read-only walk
# into a new[]/delete[] pair.  Generate a Vita overlay that preserves the
# existing compaction path after a removal while retaining stable storage for
# an unchanged list.  The host-only regression test below explicitly includes
# this overlay; ordinary host engine targets continue to use upstream Yuri.
set(KRKRVITA_YURI_OBJECT_LIST_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/yuri/src/core/utils/ObjectList.h")
file(READ "${KRKRVITA_YURI_OBJECT_LIST_SOURCE}"
    KRKRVITA_YURI_OBJECT_LIST_TEXT)
set(KRKRVITA_YURI_OBJECT_LIST_COMPACT_OLD [=[
		Count = (tjs_int)(d - Objects);
		Capacity = Count;
]=])
set(KRKRVITA_YURI_OBJECT_LIST_COMPACT_STABLE [=[
		const tjs_int compacted_count = (tjs_int)(d - Objects);
		if(compacted_count == Count) return;
		Count = compacted_count;
		Capacity = Count;
]=])
string(REPLACE
    "${KRKRVITA_YURI_OBJECT_LIST_COMPACT_OLD}"
    "${KRKRVITA_YURI_OBJECT_LIST_COMPACT_STABLE}"
    KRKRVITA_YURI_OBJECT_LIST_PATCHED
    "${KRKRVITA_YURI_OBJECT_LIST_TEXT}")
if(KRKRVITA_YURI_OBJECT_LIST_PATCHED STREQUAL
   KRKRVITA_YURI_OBJECT_LIST_TEXT)
    message(FATAL_ERROR
        "Yuri ObjectList stable Compact patch no longer applies")
endif()
string(FIND "${KRKRVITA_YURI_OBJECT_LIST_PATCHED}"
    "${KRKRVITA_YURI_OBJECT_LIST_COMPACT_OLD}"
    KRKRVITA_YURI_OBJECT_LIST_OLD_OFFSET)
if(NOT KRKRVITA_YURI_OBJECT_LIST_OLD_OFFSET LESS 0)
    message(FATAL_ERROR
        "Yuri ObjectList generated header retains the allocating Compact path")
endif()
foreach(KRKRVITA_YURI_OBJECT_LIST_CONTRACT
        "const tjs_int compacted_count = (tjs_int)(d - Objects);"
        "if(compacted_count == Count) return;"
        "Count = compacted_count;")
    string(FIND "${KRKRVITA_YURI_OBJECT_LIST_PATCHED}"
        "${KRKRVITA_YURI_OBJECT_LIST_CONTRACT}"
        KRKRVITA_YURI_OBJECT_LIST_CONTRACT_OFFSET)
    if(KRKRVITA_YURI_OBJECT_LIST_CONTRACT_OFFSET LESS 0)
        message(FATAL_ERROR
            "Yuri ObjectList generated-header contract is incomplete: ${KRKRVITA_YURI_OBJECT_LIST_CONTRACT}")
    endif()
endforeach()
set(KRKRVITA_YURI_OBJECT_LIST_GENERATED
    "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri/ObjectList.h")
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_OBJECT_LIST_GENERATED}"
    CONTENT "${KRKRVITA_YURI_OBJECT_LIST_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)
# Compile a generated copy of the pinned TJS unit. Several Yuri fixes affect
# inline headers, so generating only patched .cpp files would leave different
# semantics in callers from the base, visual, sound and plugin archives.
file(COPY "${KRKRVITA_YURI_TJS_SOURCE_DIR}/"
    DESTINATION "${KRKRVITA_YURI_TJS_GENERATED_DIR}"
    PATTERN "tjsConfig.cpp" EXCLUDE
    PATTERN "tjsString.h" EXCLUDE
    PATTERN "tjsVariant.h" EXCLUDE
    PATTERN "tjsVariant.cpp" EXCLUDE)

# Bytecode constants may contain ordinary Dictionary/Array dispatch objects as
# well as tTJSInterCodeContext objects.  Yuri's exporter used a C-style
# downcast before merely comparing pointer identity, which is undefined for an
# ordinary dispatch object and is diagnosed by UBSan on real retail scripts.
# Classify through the polymorphic hierarchy first; only an actual inter-code
# context can be looked up in the script block's context table.
set(KRKRVITA_TJS_CONST_ARRAY_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsConstArrayData.cpp")
file(READ "${KRKRVITA_TJS_CONST_ARRAY_SOURCE}"
    KRKRVITA_TJS_CONST_ARRAY_TEXT)
set(KRKRVITA_TJS_CONST_ARRAY_BAD_LOOKUP [=[
		iTJSDispatch2* obj = v.AsObjectNoAddRef();
		int index = block->GetCodeIndex( (const tTJSInterCodeContext*)obj );
]=])
set(KRKRVITA_TJS_CONST_ARRAY_SAFE_LOOKUP [=[
		iTJSDispatch2* obj = v.AsObjectNoAddRef();
		const tTJSInterCodeContext* context =
			dynamic_cast<const tTJSInterCodeContext*>(obj);
		int index = context ? block->GetCodeIndex(context) : -1;
]=])
string(REPLACE
    "${KRKRVITA_TJS_CONST_ARRAY_BAD_LOOKUP}"
    "${KRKRVITA_TJS_CONST_ARRAY_SAFE_LOOKUP}"
    KRKRVITA_TJS_CONST_ARRAY_PATCHED
    "${KRKRVITA_TJS_CONST_ARRAY_TEXT}")
if(KRKRVITA_TJS_CONST_ARRAY_PATCHED STREQUAL
   KRKRVITA_TJS_CONST_ARRAY_TEXT)
    message(FATAL_ERROR
        "Yuri bytecode object-constant classification patch no longer applies")
endif()
set(KRKRVITA_TJS_CONST_ARRAY_BAD_PUT [=[
	case TYPE_INTER_OBJECT: {
		iTJSDispatch2* obj = v.AsObjectNoAddRef();
		return block->GetCodeIndex( (const tTJSInterCodeContext*)obj );
	}
]=])
set(KRKRVITA_TJS_CONST_ARRAY_SAFE_PUT [=[
	case TYPE_INTER_OBJECT: {
		iTJSDispatch2* obj = v.AsObjectNoAddRef();
		const tTJSInterCodeContext* context =
			dynamic_cast<const tTJSInterCodeContext*>(obj);
		return context ? block->GetCodeIndex(context) : -1;
	}
]=])
set(KRKRVITA_TJS_CONST_ARRAY_BEFORE_PUT
    "${KRKRVITA_TJS_CONST_ARRAY_PATCHED}")
string(REPLACE
    "${KRKRVITA_TJS_CONST_ARRAY_BAD_PUT}"
    "${KRKRVITA_TJS_CONST_ARRAY_SAFE_PUT}"
    KRKRVITA_TJS_CONST_ARRAY_PATCHED
    "${KRKRVITA_TJS_CONST_ARRAY_PATCHED}")
if(KRKRVITA_TJS_CONST_ARRAY_PATCHED STREQUAL
   KRKRVITA_TJS_CONST_ARRAY_BEFORE_PUT)
    message(FATAL_ERROR
        "Yuri bytecode inter-code constant insertion patch no longer applies")
endif()
string(FIND "${KRKRVITA_TJS_CONST_ARRAY_PATCHED}"
    "(const tTJSInterCodeContext*)obj"
    KRKRVITA_TJS_CONST_ARRAY_UNSAFE_CAST_OFFSET)
if(NOT KRKRVITA_TJS_CONST_ARRAY_UNSAFE_CAST_OFFSET LESS 0)
    message(FATAL_ERROR
        "Yuri bytecode object-constant exporter retains an unsafe downcast")
endif()
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsConstArrayData.cpp"
    CONTENT "${KRKRVITA_TJS_CONST_ARRAY_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

# Yuri's hash table uses placement new into raw byte arrays, but those arrays
# do not carry the alignment of KeyT/ValueT. This is undefined behavior for
# cached script objects on 64-bit hosts and for any over-aligned value on the
# target. Preserve the table layout and lifetime scheme while giving both
# placement-new destinations their required C++ alignment.
set(KRKRVITA_TJS_HASH_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsHashSearch.h")
file(READ "${KRKRVITA_TJS_HASH_SOURCE}" KRKRVITA_TJS_HASH_TEXT)
set(KRKRVITA_TJS_HASH_BAD_STORAGE
    "\t\tchar Key[sizeof(KeyT)];\n\t\tchar Value[sizeof(ValueT)];")
set(KRKRVITA_TJS_HASH_FIXED_STORAGE
    "\t\talignas(KeyT) char Key[sizeof(KeyT)];\n\t\talignas(ValueT) char Value[sizeof(ValueT)];")
string(REPLACE "${KRKRVITA_TJS_HASH_BAD_STORAGE}"
    "${KRKRVITA_TJS_HASH_FIXED_STORAGE}"
    KRKRVITA_TJS_HASH_PATCHED "${KRKRVITA_TJS_HASH_TEXT}")
if(KRKRVITA_TJS_HASH_PATCHED STREQUAL KRKRVITA_TJS_HASH_TEXT)
    message(FATAL_ERROR "Yuri hash-table placement alignment patch no longer applies")
endif()
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsHashSearch.h"
    CONTENT "${KRKRVITA_TJS_HASH_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

# Yuri's bundled UTF-16 formatter contains two target-ABI assumptions. Generate
# a corrected translation unit while leaving the pinned upstream source intact.
set(KRKRVITA_TJS_CONFIG_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsConfig.cpp")
file(READ "${KRKRVITA_TJS_CONFIG_SOURCE}" KRKRVITA_TJS_CONFIG_TEXT)

# LDBL_MANT_DIG is 53 on Vita, so shifting an int by that amount is undefined.
# ldexpl expresses the intended 2^mantissa value without an integer-width
# dependency.
string(REPLACE
    "long double round = 0x1<<LDBL_MANT_DIG;"
    "long double round = ldexpl(1.0L, LDBL_MANT_DIG);"
    KRKRVITA_TJS_CONFIG_PATCHED "${KRKRVITA_TJS_CONFIG_TEXT}")
if(KRKRVITA_TJS_CONFIG_PATCHED STREQUAL KRKRVITA_TJS_CONFIG_TEXT)
    message(FATAL_ERROR "Yuri tjsConfig long-double patch no longer applies")
endif()

# A va_list function parameter is already adjusted on x86_64 SysV; taking its
# address creates a pointer-to-pointer and crashes while formatting errors.
if(NOT VITA)
    set(KRKRVITA_BAD_VA_LIST
        "va_list *pap = (va_list *)&ap;\n    r = printf_core(&f, fmt, pap, nl_arg, nl_type);")
    set(KRKRVITA_FIXED_VA_LIST
        "va_list copied;\n    va_copy(copied, ap);\n    r = printf_core(&f, fmt, &copied, nl_arg, nl_type);\n    va_end(copied);")
    string(REPLACE "${KRKRVITA_BAD_VA_LIST}" "${KRKRVITA_FIXED_VA_LIST}"
        KRKRVITA_TJS_CONFIG_HOST_PATCHED "${KRKRVITA_TJS_CONFIG_PATCHED}")
    if(KRKRVITA_TJS_CONFIG_HOST_PATCHED STREQUAL KRKRVITA_TJS_CONFIG_PATCHED)
        message(FATAL_ERROR "Yuri tjsConfig va_list patch no longer applies")
    endif()
    set(KRKRVITA_TJS_CONFIG_PATCHED "${KRKRVITA_TJS_CONFIG_HOST_PATCHED}")
endif()

# TJS_malloc stores an allocation size immediately before the returned
# pointer. TJS_free must nevertheless retain the standard free(nullptr)
# contract: the variant-array stack legitimately has no backing allocation
# when an engine is created only for compilation. Subtracting the header size
# from null is undefined and crashed during normal tTJS destruction.
set(KRKRVITA_TJS_CONFIG_BAD_FREE
    "void TJS_free(void *buf)\n{\n\tfree((char*)buf - sizeof(size_t));\n}")
set(KRKRVITA_TJS_CONFIG_FIXED_FREE
    "void TJS_free(void *buf)\n{\n\tif (!buf) return;\n\tfree((char*)buf - sizeof(size_t));\n}")
string(REPLACE "${KRKRVITA_TJS_CONFIG_BAD_FREE}"
    "${KRKRVITA_TJS_CONFIG_FIXED_FREE}"
    KRKRVITA_TJS_CONFIG_ALLOCATOR_PATCHED "${KRKRVITA_TJS_CONFIG_PATCHED}")
if(KRKRVITA_TJS_CONFIG_ALLOCATOR_PATCHED STREQUAL KRKRVITA_TJS_CONFIG_PATCHED)
    message(FATAL_ERROR "Yuri null-free allocator patch no longer applies")
endif()
set(KRKRVITA_TJS_CONFIG_PATCHED "${KRKRVITA_TJS_CONFIG_ALLOCATOR_PATCHED}")
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsConfig.cpp"
    CONTENT "${KRKRVITA_TJS_CONFIG_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

# Yuri's empty ttstr representation is a null internal string pointer.  Its
# GetLen() null check was historically restricted to CodeGuard builds, which
# makes ordinary optimized builds dereference null for a perfectly valid empty
# string (for example while formatting an error with an empty media name).
# Generate a corrected copy while keeping the pinned submodule immutable. The
# generated include directory is used by every backend archive, so inline
# string semantics cannot differ across static-library boundaries.
set(KRKRVITA_TJS_STRING_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsString.h")
file(READ "${KRKRVITA_TJS_STRING_SOURCE}" KRKRVITA_TJS_STRING_TEXT)
set(KRKRVITA_TJS_STRING_BAD_GETLEN
    "#ifdef __CODEGUARD__\n\t\tif(!Ptr) return 0; // tTJSVariantString::GetLength can return zero if 'this' is NULL\n#endif\n\t\treturn Ptr->GetLength();")
set(KRKRVITA_TJS_STRING_FIXED_GETLEN
    "if(!Ptr) return 0;\n\t\treturn Ptr->GetLength();")
string(REPLACE "${KRKRVITA_TJS_STRING_BAD_GETLEN}"
    "${KRKRVITA_TJS_STRING_FIXED_GETLEN}"
    KRKRVITA_TJS_STRING_PATCHED "${KRKRVITA_TJS_STRING_TEXT}")
if(KRKRVITA_TJS_STRING_PATCHED STREQUAL KRKRVITA_TJS_STRING_TEXT)
    message(FATAL_ERROR "Yuri empty-string length patch no longer applies")
endif()
set(KRKRVITA_TJS_STRING_COMPAT_HEADER
    "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsString.h")
file(CONFIGURE
    OUTPUT "${KRKRVITA_TJS_STRING_COMPAT_HEADER}"
    CONTENT "${KRKRVITA_TJS_STRING_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

# Apply the same empty-string invariant to tTJSVariant. Empty strings are
# represented by tvtString plus a null String pointer; every conversion must
# therefore produce the normal empty value instead of invoking a member on
# null. This is exercised by the real-script bytecode compiler test below.
set(KRKRVITA_TJS_VARIANT_HEADER_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsVariant.h")
file(READ "${KRKRVITA_TJS_VARIANT_HEADER_SOURCE}"
    KRKRVITA_TJS_VARIANT_HEADER_TEXT)
string(REPLACE
    "\t\treturn *String;"
    "\t\treturn String ? String->operator const tjs_char *() : TJSNullStrPtr;"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_TEXT}")
string(REPLACE
    "case tvtString:  return String->ToInteger();"
    "case tvtString:  return String ? String->ToInteger() : 0;"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "case tvtString:  String->ToNumber(targ); return;"
    "case tvtString:  if(String) String->ToNumber(targ); else targ = (tjs_int)0; return;"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "case tvtString:  return String->ToReal();"
    "case tvtString:  return String ? String->ToReal() : 0.0;"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "\t\t\tString->ToNumber(val);"
    "\t\t\tif(String) String->ToNumber(val); else val = (tjs_int)0;"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
set(KRKRVITA_TJS_VARIANT_HEADER_BEFORE_CONCAT
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "\t\t\tval.String = TJSAllocVariantString(*s1, *s2);"
    "\t\t\tval.String = TJSAllocVariantString(\n\t\t\t\ts1 ? s1->operator const tjs_char *() : TJSNullStrPtr,\n\t\t\t\ts2 ? s2->operator const tjs_char *() : TJSNullStrPtr);"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_BEFORE_CONCAT)
    message(FATAL_ERROR "Yuri empty-string binary-concatenation patch no longer applies")
endif()

# TJS exposes signed 64-bit integers, but its historical desktop runtime also
# inherits x86's modulo-64 variable-shift count.  Express that established bit
# behavior without signed-overflow, invalid-count, or signed-right-shift C++
# behavior so the compiler's constant folder and every runtime caller agree on
# x86, ARM and sanitizer builds.
set(KRKRVITA_TJS_VARIANT_SHIFT_DECLARATIONS [=[
TJS_EXP_FUNC_DEF(void, TJSThrowDivideByZero, ());
]=])
set(KRKRVITA_TJS_VARIANT_DEFINED_SHIFTS [=[
TJS_EXP_FUNC_DEF(void, TJSThrowDivideByZero, ());

static inline tjs_uint TJSNormalizeShiftCount(tTVInteger count)
{
	return static_cast<tjs_uint>(static_cast<tjs_uint64>(count) & 63u);
}

static inline tTVInteger TJSIntegerFromBitPattern(tjs_uint64 bits)
{
	const tjs_uint64 sign = static_cast<tjs_uint64>(1) << 63;
	if((bits & sign) == 0) return static_cast<tTVInteger>(bits);
	return -1 - static_cast<tTVInteger>(~bits);
}

static inline tTVInteger TJSShiftLeft(tTVInteger value, tTVInteger count)
{
	return TJSIntegerFromBitPattern(static_cast<tjs_uint64>(value) <<
		TJSNormalizeShiftCount(count));
}

static inline tTVInteger TJSShiftArithmeticRight(
	tTVInteger value, tTVInteger count)
{
	const tjs_uint shift = TJSNormalizeShiftCount(count);
	tjs_uint64 result = static_cast<tjs_uint64>(value) >> shift;
	if(value < 0 && shift != 0)
		result |= ~static_cast<tjs_uint64>(0) << (64u - shift);
	return TJSIntegerFromBitPattern(result);
}

static inline tTVInteger TJSShiftLogicalRight(
	tTVInteger value, tTVInteger count)
{
	return TJSIntegerFromBitPattern(static_cast<tjs_uint64>(value) >>
		TJSNormalizeShiftCount(count));
}
]=])
set(KRKRVITA_TJS_VARIANT_HEADER_BEFORE_SHIFT_HELPERS
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "${KRKRVITA_TJS_VARIANT_SHIFT_DECLARATIONS}"
    "${KRKRVITA_TJS_VARIANT_DEFINED_SHIFTS}"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_BEFORE_SHIFT_HELPERS)
    message(FATAL_ERROR "Yuri defined shift-helper patch no longer applies")
endif()

set(KRKRVITA_TJS_VARIANT_HEADER_BEFORE_ARITHMETIC_SHIFT
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "return (tTVInteger)(AsInteger()>>(tjs_int)rhs.AsInteger());"
    "return TJSShiftArithmeticRight(AsInteger(), rhs.AsInteger());"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_BEFORE_ARITHMETIC_SHIFT)
    message(FATAL_ERROR "Yuri inline arithmetic-right-shift patch no longer applies")
endif()

set(KRKRVITA_TJS_VARIANT_HEADER_BEFORE_LOGICAL_SHIFT
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "return (tTVInteger)((tjs_uint64)AsInteger()>> count);"
    "return TJSShiftLogicalRight(AsInteger(), count);"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_BEFORE_LOGICAL_SHIFT)
    message(FATAL_ERROR "Yuri inline logical-right-shift patch no longer applies")
endif()

set(KRKRVITA_TJS_VARIANT_HEADER_BEFORE_LEFT_SHIFT
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
string(REPLACE
    "return (tTVInteger)(AsInteger()<<(tjs_int)rhs.AsInteger());"
    "return TJSShiftLeft(AsInteger(), rhs.AsInteger());"
    KRKRVITA_TJS_VARIANT_HEADER_PATCHED
    "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}")
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_BEFORE_LEFT_SHIFT)
    message(FATAL_ERROR "Yuri inline left-shift patch no longer applies")
endif()

foreach(KRKRVITA_TJS_VARIANT_RAW_SHIFT IN ITEMS
        "AsInteger()>>(tjs_int)rhs.AsInteger()"
        "(tjs_uint64)AsInteger()>> count"
        "AsInteger()<<(tjs_int)rhs.AsInteger()")
    string(FIND "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}"
        "${KRKRVITA_TJS_VARIANT_RAW_SHIFT}"
        KRKRVITA_TJS_VARIANT_RAW_SHIFT_OFFSET)
    if(NOT KRKRVITA_TJS_VARIANT_RAW_SHIFT_OFFSET LESS 0)
        message(FATAL_ERROR
            "Yuri generated variant header retains a raw shift: ${KRKRVITA_TJS_VARIANT_RAW_SHIFT}")
    endif()
endforeach()
if(KRKRVITA_TJS_VARIANT_HEADER_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_HEADER_TEXT)
    message(FATAL_ERROR "Yuri empty variant-string header patch no longer applies")
endif()
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsVariant.h"
    CONTENT "${KRKRVITA_TJS_VARIANT_HEADER_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

set(KRKRVITA_TJS_VARIANT_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsVariant.cpp")
file(READ "${KRKRVITA_TJS_VARIANT_SOURCE}" KRKRVITA_TJS_VARIANT_TEXT)
string(REPLACE
    "\tif(vt == tvtString)\n\t\tString->ToNumber(*this);"
    "\tif(vt == tvtString)\n\t{\n\t\tif(String) String->ToNumber(*this);\n\t\telse { vt = tvtInteger; Integer = 0; }\n\t}"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_TEXT}")
string(REPLACE
    "\tif(vt==tvtString)\n\t{\n\t\tString->ToNumber(*this);\n\t\treturn;\n\t}"
    "\tif(vt==tvtString)\n\t{\n\t\tif(String) String->ToNumber(*this);\n\t\telse { vt = tvtInteger; Integer = 0; }\n\t\treturn;\n\t}"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
set(KRKRVITA_TJS_VARIANT_BEFORE_COMPARE
    "${KRKRVITA_TJS_VARIANT_PATCHED}")
string(REPLACE
    "\tconst tjs_char *p1 = *s1;\n\tconst tjs_char *p2 = *s2;\n\tif(!p1) p1=TJS_W(\"\");\n\tif(!p2) p2=TJS_W(\"\");"
    "\tconst tjs_char *p1 = s1 ? s1->operator const tjs_char *() : TJSNullStrPtr;\n\tconst tjs_char *p2 = s2 ? s2->operator const tjs_char *() : TJSNullStrPtr;"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_BEFORE_COMPARE)
    message(FATAL_ERROR "Yuri empty-string comparison patch no longer applies")
endif()
set(KRKRVITA_TJS_VARIANT_BEFORE_CONCAT
    "${KRKRVITA_TJS_VARIANT_PATCHED}")
string(REPLACE
    "\t\tval.String = TJSAllocVariantString(*s1, *s2);"
    "\t\tval.String = TJSAllocVariantString(\n\t\t\ts1 ? s1->operator const tjs_char *() : TJSNullStrPtr,\n\t\t\ts2 ? s2->operator const tjs_char *() : TJSNullStrPtr);"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_BEFORE_CONCAT)
    message(FATAL_ERROR "Yuri empty-string compound-concatenation patch no longer applies")
endif()

# Route compound operators through the same helpers as the inline operators.
# The VM implements ordinary binary shifts with these compound methods too, so
# this covers both bytecode paths rather than only explicit `<<=` syntax.
set(KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_ARITHMETIC_SHIFT
    "${KRKRVITA_TJS_VARIANT_PATCHED}")
string(REPLACE
    "\tInteger=l>>(tjs_int)rhs.AsInteger();"
    "\tInteger=TJSShiftArithmeticRight(l, rhs.AsInteger());"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_ARITHMETIC_SHIFT)
    message(FATAL_ERROR "Yuri compound arithmetic-right-shift patch no longer applies")
endif()

set(KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_LOGICAL_SHIFT
    "${KRKRVITA_TJS_VARIANT_PATCHED}")
string(REPLACE
    "\tInteger=(tjs_int64)((tjs_uint64)l>> (tjs_int)rhs);"
    "\tInteger=TJSShiftLogicalRight(l, rhs.AsInteger());"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_LOGICAL_SHIFT)
    message(FATAL_ERROR "Yuri compound logical-right-shift patch no longer applies")
endif()

set(KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_LEFT_SHIFT
    "${KRKRVITA_TJS_VARIANT_PATCHED}")
string(REPLACE
    "\tInteger=l<<(tjs_int)rhs.AsInteger();"
    "\tInteger=TJSShiftLeft(l, rhs.AsInteger());"
    KRKRVITA_TJS_VARIANT_PATCHED "${KRKRVITA_TJS_VARIANT_PATCHED}")
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL
   KRKRVITA_TJS_VARIANT_BEFORE_COMPOUND_LEFT_SHIFT)
    message(FATAL_ERROR "Yuri compound left-shift patch no longer applies")
endif()

foreach(KRKRVITA_TJS_VARIANT_RAW_SHIFT IN ITEMS
        "Integer=l>>(tjs_int)rhs.AsInteger();"
        "Integer=(tjs_int64)((tjs_uint64)l>> (tjs_int)rhs);"
        "Integer=l<<(tjs_int)rhs.AsInteger();")
    string(FIND "${KRKRVITA_TJS_VARIANT_PATCHED}"
        "${KRKRVITA_TJS_VARIANT_RAW_SHIFT}"
        KRKRVITA_TJS_VARIANT_RAW_SHIFT_OFFSET)
    if(NOT KRKRVITA_TJS_VARIANT_RAW_SHIFT_OFFSET LESS 0)
        message(FATAL_ERROR
            "Yuri generated variant source retains a raw shift: ${KRKRVITA_TJS_VARIANT_RAW_SHIFT}")
    endif()
endforeach()
if(KRKRVITA_TJS_VARIANT_PATCHED STREQUAL KRKRVITA_TJS_VARIANT_TEXT)
    message(FATAL_ERROR "Yuri empty variant-string source patch no longer applies")
endif()
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsVariant.cpp"
    CONTENT "${KRKRVITA_TJS_VARIANT_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

# Empty TJS strings use a null tTJSVariantString pointer.  The bytecode
# interpreter's string-property helper guarded that representation only when
# built with the obsolete CodeGuard compiler, then invoked GetLength through
# null in every normal Vita build.  This is the exact fault captured when a
# retail script evaluated an empty string's `length` property.  Preserve TJS's
# established semantics (empty length is zero, index zero yields empty) in all
# builds and in every property addressing form.
set(KRKRVITA_TJS_INTERCODE_SOURCE
    "${KRKRVITA_YURI_TJS_SOURCE_DIR}/tjsInterCodeExec.cpp")
file(READ "${KRKRVITA_TJS_INTERCODE_SOURCE}" KRKRVITA_TJS_INTERCODE_TEXT)
set(KRKRVITA_TJS_INTERCODE_BAD_LENGTH [=[
			const tTJSVariantString * s = str->AsStringNoAddRef();
#ifdef __CODEGUARD__
			if(!s)
				*result = tTVInteger(0); // tTJSVariantString::GetLength can return zero if 'this' is NULL
			else
#endif
			*result = tTVInteger(s->GetLength());
]=])
set(KRKRVITA_TJS_INTERCODE_FIXED_LENGTH [=[
			const tTJSVariantString * s = str->AsStringNoAddRef();
			*result = tTVInteger(s ? s->GetLength() : 0);
]=])
string(REPLACE "${KRKRVITA_TJS_INTERCODE_BAD_LENGTH}"
    "${KRKRVITA_TJS_INTERCODE_FIXED_LENGTH}"
    KRKRVITA_TJS_INTERCODE_PATCHED "${KRKRVITA_TJS_INTERCODE_TEXT}")
if(KRKRVITA_TJS_INTERCODE_PATCHED STREQUAL KRKRVITA_TJS_INTERCODE_TEXT)
    message(FATAL_ERROR "Yuri empty-string property-length patch no longer applies")
endif()
set(KRKRVITA_TJS_INTERCODE_BEFORE_INDEX "${KRKRVITA_TJS_INTERCODE_PATCHED}")
string(REPLACE
    "tjs_int len = valstr->GetLength();"
    "tjs_int len = valstr ? valstr->GetLength() : 0;"
    KRKRVITA_TJS_INTERCODE_PATCHED "${KRKRVITA_TJS_INTERCODE_PATCHED}")
if(KRKRVITA_TJS_INTERCODE_PATCHED STREQUAL
   KRKRVITA_TJS_INTERCODE_BEFORE_INDEX)
    message(FATAL_ERROR "Yuri empty-string property-index patch no longer applies")
endif()
file(CONFIGURE
    OUTPUT "${KRKRVITA_YURI_TJS_GENERATED_DIR}/tjsInterCodeExec.cpp"
    CONTENT "${KRKRVITA_TJS_INTERCODE_PATCHED}"
    @ONLY NEWLINE_STYLE UNIX)

file(GLOB KRKRVITA_YURI_TJS_SOURCES CONFIGURE_DEPENDS
    "${KRKRVITA_YURI_TJS_GENERATED_DIR}/*.cpp")

add_library(krkrvita-yuri-tjs STATIC
    ${KRKRVITA_YURI_TJS_SOURCES}
    src/yuri/tjs_platform.cpp
)
set_target_properties(krkrvita-yuri-tjs PROPERTIES CXX_STANDARD 17)
target_include_directories(krkrvita-yuri-tjs PUBLIC
    src/yuri/compat
    "${KRKRVITA_YURI_TJS_GENERATED_DIR}"
    vendor/yuri/src/core/environ
    vendor/yuri/src/core/utils
)
target_compile_definitions(krkrvita-yuri-tjs PUBLIC
    TJS_TEXT_OUT_CRLF
    USE_UNICODE_FSTRING
)
target_compile_options(krkrvita-yuri-tjs PRIVATE
    -include${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/tjs_compat.hpp
    -Wno-deprecated-declarations
    -Wno-implicit-fallthrough
)

set(KRKRVITA_COMMON_SOURCES
    src/common/bubble.cpp
    src/common/filter_heuristic.cpp
    src/common/game.cpp
    src/common/patch_manifest.cpp
    src/common/patch_repository.cpp
    src/common/phase1_filter.cpp
    src/common/pe_resources.cpp
    src/common/png.cpp
    src/common/profile.cpp
    src/engine/retail/psb.cpp
    src/common/retail_filter.cpp
    src/common/sfo.cpp
    src/common/sha256.cpp
    src/common/storage.cpp
    src/common/text_codec.cpp
    src/common/xp3_archive.cpp
    src/yuri/xp3_filter_vm.cpp
)
if(NOT VITA)
    list(APPEND KRKRVITA_COMMON_SOURCES
        src/common/native_plugin_inventory.cpp)
endif()

add_library(krkrvita-common STATIC ${KRKRVITA_COMMON_SOURCES})
target_include_directories(krkrvita-common PUBLIC include)
target_compile_options(krkrvita-common PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(krkrvita-common PUBLIC krkrvita-yuri-tjs)

# sqlite3.dll is used by KAG environment databases for prepared statements,
# attached read-only databases, and BLOB scene state.  Build the same official
# amalgamation on host and Vita so the compatibility test and target do not
# silently exercise different SQL engines.
add_library(krkrvita-sqlite STATIC third_party/sqlite/sqlite3.c)
if(VITA)
    target_sources(krkrvita-sqlite PRIVATE
        src/platform/vita/sqlite_posix_compat.c)
endif()
target_include_directories(krkrvita-sqlite BEFORE PRIVATE src/yuri/compat)
target_include_directories(krkrvita-sqlite PUBLIC third_party/sqlite)
target_compile_definitions(krkrvita-sqlite PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_OMIT_WAL=1
    SQLITE_MAX_MMAP_SIZE=0
    SQLITE_TEMP_STORE=3)
set_target_properties(krkrvita-sqlite PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON)

if(VITA)
    # Runtime preparation includes exact pinned-patch resolution plus the
    # archive-only Phase 1 detector. It does not download content or perform
    # Phase 2 executable analysis; an inconclusive archive reports that later
    # phase explicitly instead of selecting a guessed catch-all filter.
    add_library(krkrvita-retail-resolver STATIC
        src/common/filter_heuristic.cpp
        src/common/game.cpp
        src/common/patch_manifest.cpp
        src/common/phase1_filter.cpp
        src/common/pe_resources.cpp
        src/common/sha256.cpp
        src/common/xp3_archive.cpp
        src/yuri/xp3_filter_vm.cpp
    )
    target_include_directories(krkrvita-retail-resolver PUBLIC include)
    target_compile_options(krkrvita-retail-resolver PRIVATE
        -Wall -Wextra -Wpedantic)
    target_link_libraries(krkrvita-retail-resolver PUBLIC
        krkrvita-yuri-tjs
        z)

    include(FetchContent)
    FetchContent_Declare(krkrvita_retail_patches
        GIT_REPOSITORY https://github.com/zeas2/Kirikiroid2_patch.git
        GIT_TAG 0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
    )
    FetchContent_MakeAvailable(krkrvita_retail_patches)
    set(KRKRVITA_PATCH_SOURCE_DIR
        "${krkrvita_retail_patches_SOURCE_DIR}"
        CACHE INTERNAL "Pinned Kirikiroid2 retail patch snapshot")

    # The sample's KAG scripts use the complete Kirikiri 2 extrans transition
    # set, not just the three handlers built into the core. Fetch the original
    # plugin sources at a fixed revision; YuriBackend.cmake adapts only the
    # DLL/import boundary and the x86-only ripple kernels for static ARM use.
    FetchContent_Declare(krkrvita_extrans
        GIT_REPOSITORY https://github.com/krkrz/SamplePlugin.git
        GIT_TAG d94e312065f7298a970b58bfe73de48e52aef9ee
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
    )
    FetchContent_MakeAvailable(krkrvita_extrans)
    set(KRKRVITA_EXTRANS_SOURCE_DIR
        "${krkrvita_extrans_SOURCE_DIR}/extrans"
        CACHE INTERNAL "Pinned Kirikiri extrans source directory")

    FetchContent_Declare(krkrvita_layerexsave
        GIT_REPOSITORY https://github.com/wtnbgo/layerExSave.git
        GIT_TAG b53d5bf6616fcaf31c8a095ff414b17e4274445e
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
        SOURCE_SUBDIR krkrvita-fetch-only
    )
    FetchContent_MakeAvailable(krkrvita_layerexsave)
    set(KRKRVITA_LAYEREXSAVE_SOURCE_DIR
        "${krkrvita_layerexsave_SOURCE_DIR}"
        CACHE INTERNAL "Pinned Kirikiri layerExSave source directory")

    # LayerExImage is a common KAGEX dependency which extends Layer with the
    # pixel effects used by brightness, colour and blur animation paths. Keep
    # the original implementation pinned and adapt it into Yuri's static
    # internal-plugin registry in the Vita backend.
    FetchContent_Declare(krkrvita_layereximage
        GIT_REPOSITORY https://github.com/wtnbgo/layerExImage.git
        GIT_TAG 98c53ec33f5d99cea007eb5397fffd1321f34343
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
        SOURCE_SUBDIR krkrvita-fetch-only
    )
    FetchContent_MakeAvailable(krkrvita_layereximage)
    set(KRKRVITA_LAYEREXIMAGE_SOURCE_DIR
        "${krkrvita_layereximage_SOURCE_DIR}"
        CACHE INTERNAL "Pinned Kirikiri layerExImage source directory")

    # Squirrel is a real KAG extension, not a Windows-only optional cosmetic
    # plugin: KAGEX titles use its fork/drive/wait API for scripted motion and
    # effects.  Reuse the upstream Kirikiri implementation and adapt only its
    # TJS/storage boundary in YuriBackend.cmake.  Pin the exact source because
    # the VM bytecode and TJS bridge are part of the compatibility contract.
    FetchContent_Declare(krkrvita_squirrel
        GIT_REPOSITORY https://github.com/krkrz/krkrz.git
        GIT_TAG 49c4d53506edecb824cd7b2cff8d32959b1f1b70
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
    )
    FetchContent_MakeAvailable(krkrvita_squirrel)
    set(KRKRVITA_SQUIRREL_SOURCE_DIR
        "${krkrvita_squirrel_SOURCE_DIR}/src/plugins/win32/squirrel"
        CACHE INTERNAL "Pinned Kirikiri Squirrel source directory")

    # Torikago and Noble Works use psd.dll for their layered PSD scene
    # backgrounds. Reuse the maintained Kirikiri PSD parser/plugin source;
    # YuriBackend.cmake only adapts its storage/registration boundary and
    # keeps the parser itself authoritative.
    FetchContent_Declare(krkrvita_psdfile
        GIT_REPOSITORY https://github.com/2468785842/krkr2.git
        GIT_TAG dca7264572a63e753c1b07ca7053513d1751ce70
        GIT_SHALLOW TRUE
        GIT_PROGRESS FALSE
        SOURCE_SUBDIR krkrvita-fetch-only
    )
    FetchContent_MakeAvailable(krkrvita_psdfile)
    set(KRKRVITA_PSDFILE_SOURCE_DIR
        "${krkrvita_psdfile_SOURCE_DIR}/cpp/plugins/psdfile"
        CACHE INTERNAL "Pinned Kirikiri PSD plugin source directory")

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_TEST OFF CACHE BOOL "Build Oniguruma tests" FORCE)
    set(INSTALL_DOCUMENTATION OFF CACHE BOOL "Install Oniguruma docs" FORCE)
    set(INSTALL_EXAMPLES OFF CACHE BOOL "Install Oniguruma examples" FORCE)
    add_subdirectory(vendor/oniguruma EXCLUDE_FROM_ALL)
    target_compile_definitions(krkrvita-yuri-tjs PUBLIC
        KRKRVITA_USE_ONIGURUMA=1)
    target_link_libraries(krkrvita-yuri-tjs PUBLIC onig)
    include("${VITASDK}/share/vita.cmake")
    include(cmake/YuriBackend.cmake)
    krkrvita_add_yuri_backend_targets()
else()
    # A matching header/library pair matters here: Ruby's private Onigmo
    # header is not ABI-compatible with a distribution libonig. Pinning the
    # small host dependency also makes regex-enabled script probes repeatable.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_TEST OFF CACHE BOOL "Build Oniguruma tests" FORCE)
    set(INSTALL_DOCUMENTATION OFF CACHE BOOL "Install Oniguruma docs" FORCE)
    set(INSTALL_EXAMPLES OFF CACHE BOOL "Install Oniguruma examples" FORCE)
    add_subdirectory(vendor/oniguruma EXCLUDE_FROM_ALL)
    target_compile_definitions(krkrvita-yuri-tjs PUBLIC
        KRKRVITA_USE_ONIGURUMA=1)
    target_link_libraries(krkrvita-yuri-tjs PUBLIC onig)
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(PNG REQUIRED)
    find_package(SQLite3 REQUIRED)
    find_package(ZLIB REQUIRED)
    target_link_libraries(krkrvita-common PUBLIC CURL::libcurl PNG::PNG ZLIB::ZLIB)

    add_executable(krkrvita-tool src/tool/main.cpp)
    target_link_libraries(krkrvita-tool PRIVATE krkrvita-common)

    if(KRKRVITA_BUILD_TESTS)
        enable_testing()
        add_executable(krkrvita-tests tests/test_main.cpp)
        target_link_libraries(krkrvita-tests PRIVATE krkrvita-common)
        target_compile_definitions(krkrvita-tests PRIVATE
            KRKRVITA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME krkrvita-tests COMMAND krkrvita-tests)

        # A retail compatibility result must exercise the same generated XP3
        # filter and TJS compiler as the Vita backend.  This executable audits
        # one immutable game fingerprint at a time: every TJS unit and every
        # KAG inline-script block is compiled, every supported image/audio
        # payload is signature-checked, and boot-reachable plugin requests plus
        # unsupported runtime formats are rejected from a shared inventory. The
        # concrete local corpus is registered below when its directories are
        # available; CI can provide the same paths through the cache list.
        add_executable(krkrvita-retail-compatibility
            tests/test_retail_compatibility.cpp)
        target_link_libraries(krkrvita-retail-compatibility PRIVATE
            krkrvita-common)
        add_test(NAME krkrvita-retail-compatibility-contracts
            COMMAND krkrvita-retail-compatibility --self-test)
        set_tests_properties(krkrvita-retail-compatibility-contracts
            PROPERTIES LABELS "retail;compatibility")
        add_executable(krkrvita-retail-armv7-runtime
            tests/retail_armv7_runtime.cpp)
        target_include_directories(krkrvita-retail-armv7-runtime PRIVATE include)
        target_link_libraries(krkrvita-retail-armv7-runtime PRIVATE
            krkrvita-yuri-tjs)
        set(KRKRVITA_RETAIL_COMPATIBILITY_MANIFEST
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/retail_compatibility_manifest.txt"
            CACHE FILEPATH "Local immutable retail compatibility manifest")
        if(EXISTS "${KRKRVITA_RETAIL_COMPATIBILITY_MANIFEST}")
            add_custom_target(krkrvita-final-compatibility-gate
                COMMAND "${CMAKE_COMMAND}" -E env
                    "KRKRVITA_RETAIL_RUNNER=$<TARGET_FILE:krkrvita-retail-compatibility>"
                    "KRKRVITA_RETAIL_MANIFEST=${KRKRVITA_RETAIL_COMPATIBILITY_MANIFEST}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run-retail-compatibility.sh"
                    --require-all
                DEPENDS krkrvita-retail-compatibility
                USES_TERMINAL
                COMMENT "Running fail-closed retail compatibility gate")

            # The three gate titles which actually ship movies cover both
            # legacy families required by the Vita backend: MPEG-PS with
            # MPEG-1/MP2 and ASF with WMV3/WMA2. Decode their real encrypted-
            # archive payloads on the host and compare deterministic RGBA/PCM
            # output; the Vita ELF verifier separately requires those exact
            # decoder and demuxer symbols in the cross-linked executable.
            set(krkrvita_chichimiko_archive
                "/mnt/j/rapapuru/chichimiko/video.xp3")
            set(krkrvita_pl0002_archive "/mnt/j/PL-0002/data.xp3")
            set(krkrvita_lpk30008_archive
                "/mnt/j/lilith/LPK-30008/data.xp3")
            if(EXISTS "${krkrvita_chichimiko_archive}" AND
               EXISTS "${krkrvita_pl0002_archive}" AND
               EXISTS "${krkrvita_lpk30008_archive}")
                find_program(KRKRVITA_RETAIL_MOVIE_FFMPEG ffmpeg REQUIRED)
                add_custom_target(krkrvita-retail-movie-gate
                    COMMAND "${CMAKE_COMMAND}"
                        -DTOOL=$<TARGET_FILE:krkrvita-tool>
                        -DFFMPEG=${KRKRVITA_RETAIL_MOVIE_FFMPEG}
                        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/retail-movie-gate
                        -DCHICHIMIKO_ARCHIVE=${krkrvita_chichimiko_archive}
                        -DPL0002_ARCHIVE=${krkrvita_pl0002_archive}
                        -DLPK30008_ARCHIVE=${krkrvita_lpk30008_archive}
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRetailMovies.cmake"
                    DEPENDS krkrvita-tool
                    USES_TERMINAL
                    COMMENT "Decoding pinned retail MPEG/WMV compatibility samples")
                add_dependencies(krkrvita-final-compatibility-gate
                    krkrvita-retail-movie-gate)
                add_test(NAME krkrvita-retail-movie-compatibility
                    COMMAND "${CMAKE_COMMAND}"
                        -DTOOL=$<TARGET_FILE:krkrvita-tool>
                        -DFFMPEG=${KRKRVITA_RETAIL_MOVIE_FFMPEG}
                        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/retail-movie-ctest
                        -DCHICHIMIKO_ARCHIVE=${krkrvita_chichimiko_archive}
                        -DPL0002_ARCHIVE=${krkrvita_pl0002_archive}
                        -DLPK30008_ARCHIVE=${krkrvita_lpk30008_archive}
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRetailMovies.cmake")
                set_tests_properties(krkrvita-retail-movie-compatibility
                    PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE
                    LABELS "retail;compatibility;movie")
            endif()
            file(STRINGS "${KRKRVITA_RETAIL_COMPATIBILITY_MANIFEST}"
                krkrvita_retail_compatibility_rows ENCODING UTF-8)
            foreach(krkrvita_retail_row IN LISTS
                    krkrvita_retail_compatibility_rows)
                if(krkrvita_retail_row MATCHES "^#" OR
                   krkrvita_retail_row STREQUAL "")
                    continue()
                endif()
                string(REPLACE "|" ";" krkrvita_retail_fields
                    "${krkrvita_retail_row}")
                list(LENGTH krkrvita_retail_fields krkrvita_retail_field_count)
                if(NOT krkrvita_retail_field_count EQUAL 8)
                    message(FATAL_ERROR
                        "Malformed retail compatibility row (${krkrvita_retail_field_count} fields): ${krkrvita_retail_row}")
                endif()
                list(GET krkrvita_retail_fields 0 krkrvita_retail_id)
                list(GET krkrvita_retail_fields 1 krkrvita_retail_path)
                list(GET krkrvita_retail_fields 2 krkrvita_retail_fingerprint)
                list(GET krkrvita_retail_fields 3 krkrvita_retail_expectation)
                list(GET krkrvita_retail_fields 4 krkrvita_retail_rule)
                list(GET krkrvita_retail_fields 5 krkrvita_retail_recognized)
                list(GET krkrvita_retail_fields 6 krkrvita_retail_samples)
                list(GET krkrvita_retail_fields 7 krkrvita_retail_diagnostic)
                if(NOT EXISTS "${krkrvita_retail_path}")
                    continue()
                endif()
                if(krkrvita_retail_expectation STREQUAL "phase1")
                    set(krkrvita_retail_test_name
                        "krkrvita-retail-compatibility-${krkrvita_retail_id}")
                    add_test(NAME "${krkrvita_retail_test_name}"
                        COMMAND krkrvita-retail-compatibility --game
                            "${krkrvita_retail_path}"
                            "${krkrvita_retail_fingerprint}"
                            "${krkrvita_retail_rule}"
                            "${krkrvita_retail_recognized}"
                            "${krkrvita_retail_samples}")
                elseif(krkrvita_retail_expectation STREQUAL "runtime_blocked")
                    set(krkrvita_retail_test_name
                        "krkrvita-retail-runtime-blocked-${krkrvita_retail_id}")
                    add_test(NAME "${krkrvita_retail_test_name}"
                        COMMAND krkrvita-retail-compatibility
                            --expect-runtime-blocker
                            "${krkrvita_retail_path}"
                            "${krkrvita_retail_fingerprint}"
                            "${krkrvita_retail_rule}"
                            "${krkrvita_retail_recognized}"
                            "${krkrvita_retail_samples}"
                            "${krkrvita_retail_diagnostic}")
                elseif(krkrvita_retail_expectation STREQUAL "hardware_blocked")
                    # The host audit must keep passing; the actual blocker is
                    # recorded in tests/retail_hardware_evidence.txt and is only
                    # observable on the device.
                    set(krkrvita_retail_test_name
                        "krkrvita-retail-hardware-blocked-${krkrvita_retail_id}")
                    add_test(NAME "${krkrvita_retail_test_name}"
                        COMMAND krkrvita-retail-compatibility --game
                            "${krkrvita_retail_path}"
                            "${krkrvita_retail_fingerprint}"
                            "${krkrvita_retail_rule}"
                            "${krkrvita_retail_recognized}"
                            "${krkrvita_retail_samples}")
                elseif(krkrvita_retail_expectation STREQUAL "phase2")
                    set(krkrvita_retail_test_name
                        "krkrvita-retail-phase2-${krkrvita_retail_id}")
                    add_test(NAME "${krkrvita_retail_test_name}"
                        COMMAND krkrvita-retail-compatibility --expect-phase2
                            "${krkrvita_retail_path}"
                            "${krkrvita_retail_fingerprint}")
                else()
                    message(FATAL_ERROR
                        "Unknown retail expectation: ${krkrvita_retail_expectation}")
                endif()
                set_tests_properties(
                    "${krkrvita_retail_test_name}"
                    PROPERTIES TIMEOUT 900 LABELS "retail;compatibility")
            endforeach()
        endif()

        # Exercise the exact generated ObjectList overlay used by the Vita
        # backend.  The test counts backing-array allocations across stable
        # safe locks and then verifies that an in-lock removal still takes a
        # snapshot and is compacted at the next traversal boundary.
        add_executable(krkrvita-yuri-object-list-test
            tests/test_yuri_object_list.cpp)
        target_include_directories(krkrvita-yuri-object-list-test BEFORE PRIVATE
            "${CMAKE_CURRENT_BINARY_DIR}/generated/yuri"
            "${KRKRVITA_YURI_TJS_GENERATED_DIR}"
            vendor/yuri/src/core/utils)
        add_test(NAME krkrvita-yuri-object-list
            COMMAND krkrvita-yuri-object-list-test)

        # Yuri's timers post zero-argument TJS events. Exercise the exact
        # argument-storage and delivery helpers used by the generated Vita
        # EventIntf overlay: the empty path must pass a null argv without an
        # allocation, while nonempty ordering and exception cleanup stay intact.
        add_executable(krkrvita-yuri-event-arguments-test
            tests/test_yuri_event_arguments.cpp)
        target_include_directories(krkrvita-yuri-event-arguments-test PRIVATE
            include)
        add_test(NAME krkrvita-yuri-event-arguments
            COMMAND krkrvita-yuri-event-arguments-test)

        # Retail KAG scripts request verbose parser logging unconditionally.
        # Pin the Vita policy that retains scenario-level diagnostics while
        # suppressing only the synchronous per-tag/macro/call-stack flood.
        add_executable(krkrvita-kag-log-policy-test
            tests/test_kag_log_policy.cpp)
        target_include_directories(krkrvita-kag-log-policy-test PRIVATE
            include)
        add_test(NAME krkrvita-kag-log-policy
            COMMAND krkrvita-kag-log-policy-test)

        # Squirrel is compiled for Vita with a UTF-16 char16_t bridge rather
        # than the host's 32-bit wchar_t ABI. Exercise the exact bridge used
        # by the generated VM: string/search/token APIs, UTF-8 path conversion,
        # numeric parsing, and the bounded formatter used by printFunc.
        add_executable(krkrvita-yuri-squirrel-compat-test
            tests/test_yuri_squirrel_compat.cpp)
        target_include_directories(krkrvita-yuri-squirrel-compat-test PRIVATE
            src/engine/retail)
        add_test(NAME krkrvita-yuri-squirrel-compat
            COMMAND krkrvita-yuri-squirrel-compat-test)

        # Large retail [iscript] blocks must be assembled with one bounded
        # allocation before the TJS callback. This pins byte parity and guards
        # against Yuri's former append-per-line allocation growth.
        add_executable(krkrvita-kag-inline-script-test
            tests/test_kag_inline_script.cpp)
        target_include_directories(krkrvita-kag-inline-script-test PRIVATE
            include)
        add_test(NAME krkrvita-kag-inline-script
            COMMAND krkrvita-kag-inline-script-test)

        # sigcheck.dll validates loose XP3 files with SHA-256/RSA-PSS while
        # scripts continue booting. Pin its exact envelope, streaming verifier,
        # tamper rejection and (when installed) the retail data.xp3 signature.
        add_executable(krkrvita-rsa-pss-signature-test
            tests/test_rsa_pss_signature.cpp
            src/engine/retail/rsa_pss_signature.cpp)
        target_include_directories(krkrvita-rsa-pss-signature-test PRIVATE
            include)
        target_link_libraries(krkrvita-rsa-pss-signature-test PRIVATE
            OpenSSL::Crypto)
        add_test(NAME krkrvita-rsa-pss-signature
            COMMAND krkrvita-rsa-pss-signature-test)

        # psbfile.dll consumes both ordinary PSB assets and MDF/zlib scene
        # blobs returned by SQLite. Exercise a synthetic boundary fixture plus
        # the installed Noble Works animation and scene databases when present.
        add_executable(krkrvita-psb-test tests/test_psb.cpp)
        target_link_libraries(krkrvita-psb-test PRIVATE
            krkrvita-common SQLite::SQLite3)
        add_test(NAME krkrvita-psb COMMAND krkrvita-psb-test)

        # Compile the exact sqlite3.dll adapter used by Vita against Yuri's
        # real TJS VM.  This pins the official KiriKiri statement API rather
        # than treating a successful SQLite C-library build as compatibility.
        add_executable(krkrvita-yuri-sqlite-module-test
            tests/test_yuri_sqlite_module.cpp
            src/engine/retail/yuri_sqlite_module.cpp
            vendor/yuri/src/plugins/ncbind/ncbind.cpp)
        target_include_directories(krkrvita-yuri-sqlite-module-test BEFORE PRIVATE
            include
            src/yuri/compat
            vendor/yuri/src/core/base
            vendor/yuri/src/core/base/win32
            vendor/yuri/src/core/environ
            vendor/yuri/src/core/msg
            vendor/yuri/src/core/msg/win32
            vendor/yuri/src/core/tjs2
            vendor/yuri/src/core/utils
            vendor/yuri/src/core/utils/win32
            vendor/yuri/src/plugins)
        target_compile_options(krkrvita-yuri-sqlite-module-test PRIVATE
            -include${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/tjs_compat.hpp
            -Wno-deprecated-declarations)
        target_link_libraries(krkrvita-yuri-sqlite-module-test PRIVATE
            krkrvita-yuri-tjs krkrvita-sqlite)
        add_test(NAME krkrvita-yuri-sqlite-module
            COMMAND krkrvita-yuri-sqlite-module-test)

        # Docs, the executable manifest, and physical-Vita receipts must agree.
        # A blocked hardware receipt outranks every host result, so this test
        # is what stops a title being described as compatible while the device
        # says otherwise.
        add_test(NAME krkrvita-compatibility-claims
            COMMAND "${CMAKE_COMMAND}"
                -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/retail_compatibility_manifest.txt
                -DEVIDENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/retail_hardware_evidence.txt
                -DGATE_DOC=${CMAKE_CURRENT_SOURCE_DIR}/docs/RETAIL-COMPATIBILITY-GATE.md
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyCompatibilityClaims.cmake")
        set_tests_properties(krkrvita-compatibility-claims
            PROPERTIES LABELS "compatibility")

        # Vita seals plugin loading, so Plugins.link() succeeds only when a
        # module is present in the ncbAutoRegister registry. Compiling the
        # fallback source and finding its marker string in the ELF proves
        # neither registration nor that the surface literal executes. Drive the
        # real registry and the real TJS VM instead.
        add_executable(krkrvita-yuri-plugin-registry-test
            tests/test_yuri_plugin_registry.cpp
            src/engine/retail/yuri_motionplayer_module.cpp
            src/engine/retail/yuri_layerexdraw_module.cpp
            third_party/scriptsEx/scriptsEx.cpp
            src/engine/retail/yuri_scriptsex_module.cpp
            vendor/yuri/src/core/utils/md5.c
            vendor/yuri/src/plugins/ncbind/ncbind.cpp)
        target_include_directories(krkrvita-yuri-plugin-registry-test
            BEFORE PRIVATE
            include
            src/yuri/compat
            vendor/yuri/src/core/base
            vendor/yuri/src/core/base/win32
            vendor/yuri/src/core/environ
            vendor/yuri/src/core/msg
            vendor/yuri/src/core/msg/win32
            vendor/yuri/src/core/tjs2
            vendor/yuri/src/core/utils
            vendor/yuri/src/core/utils/win32
            vendor/yuri/src/plugins)
        target_compile_options(krkrvita-yuri-plugin-registry-test PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/src/yuri/tjs_compat.hpp>
            -Wno-deprecated-declarations)
        target_link_libraries(krkrvita-yuri-plugin-registry-test PRIVATE
            krkrvita-yuri-tjs)
        add_test(NAME krkrvita-yuri-plugin-registry
            COMMAND krkrvita-yuri-plugin-registry-test)

        # saveStruct emits quoted dictionary strings one code unit at a time.
        # The Vita plugin aggregates those writes into a fixed buffer; pin the
        # exact output and syscall-count reduction at the recovered datasu size.
        add_executable(krkrvita-buffered-units-test
            tests/test_buffered_units.cpp)
        target_include_directories(krkrvita-buffered-units-test PRIVATE include)
        add_test(NAME krkrvita-buffered-units
            COMMAND krkrvita-buffered-units-test)

        # The Vita compositor keeps small render primitives on one task and
        # admits only large work to its persistent worker pool. Verify that
        # the serial boundary invokes the original closure without an owning
        # std::function allocation, while the parallel adapter still owns and
        # executes every row partition.
        add_executable(krkrvita-render-task-dispatch-test
            tests/test_render_task_dispatch.cpp)
        target_include_directories(krkrvita-render-task-dispatch-test PRIVATE
            include)
        add_test(NAME krkrvita-render-task-dispatch
            COMMAND krkrvita-render-task-dispatch-test)

        # The Vita build replaces Android OpenCV with a compact compatibility
        # header. Pin its resize, box-filter, affine, and perspective pixel
        # contracts (including pitched external destinations and row-split
        # dispatch) to OpenCV 4.7 oracle outputs.
        add_executable(krkrvita-yuri-opencv-compat-test
            tests/test_yuri_opencv_compat.cpp)
        target_include_directories(krkrvita-yuri-opencv-compat-test PRIVATE
            src/yuri/compat)
        add_test(NAME krkrvita-yuri-opencv-compat
            COMMAND krkrvita-yuri-opencv-compat-test)

        # msgothic.ttc is user-supplied and must never be packaged. When it is
        # present in a personal checkout, compare the bounded cached stream and
        # prepared-glyph path against FreeType's ordinary two-load path using
        # face zero, including exact raster bytes and metrics.
        find_package(Freetype QUIET)
        if(Freetype_FOUND AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/msgothic.ttc")
            add_executable(krkrvita-ms-gothic-golden
                tests/test_ms_gothic_freetype.cpp)
            target_include_directories(krkrvita-ms-gothic-golden PRIVATE
                include)
            target_link_libraries(krkrvita-ms-gothic-golden PRIVATE
                Freetype::Freetype)
            add_test(NAME krkrvita-ms-gothic-golden
                COMMAND krkrvita-ms-gothic-golden
                    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/msgothic.ttc")
        endif()
        add_test(NAME krkrvita-vita-trace-contract
            COMMAND "${CMAKE_COMMAND}"
                -DTRACE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vita_boot_success.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyHardwareBootTrace.cmake)
        add_test(NAME krkrvita-vita-trace-rejects-partial
            COMMAND "${CMAKE_COMMAND}"
                -DTRACE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vita_boot_partial.txt
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyHardwareBootTrace.cmake)
        set_tests_properties(krkrvita-vita-trace-rejects-partial PROPERTIES
            WILL_FAIL TRUE)

        # Exercise the proprietary-font packaging boundary without copying the
        # user's real font. Both fixtures live only in the host build tree.
        set(krkrvita_ttc_guard_clean_dir
            "${CMAKE_CURRENT_BINARY_DIR}/tests/ttc-guard-clean")
        set(krkrvita_ttc_guard_bad_dir
            "${CMAKE_CURRENT_BINARY_DIR}/tests/ttc-guard-bad")
        file(MAKE_DIRECTORY
            "${krkrvita_ttc_guard_clean_dir}"
            "${krkrvita_ttc_guard_bad_dir}")
        file(WRITE "${krkrvita_ttc_guard_clean_dir}/payload.txt"
            "allowed payload\n")
        file(WRITE "${krkrvita_ttc_guard_bad_dir}/font.TtC"
            "synthetic test marker, not a font\n")
        add_test(NAME krkrvita-package-allows-non-ttc
            COMMAND "${CMAKE_COMMAND}"
                -DTTC_SCAN_ROOT=${krkrvita_ttc_guard_clean_dir}
                -DTTC_SCAN_CONTEXT=Test-package
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNoTtcPayload.cmake)
        add_test(NAME krkrvita-package-rejects-ttc
            COMMAND "${CMAKE_COMMAND}"
                -DTTC_SCAN_ROOT=${krkrvita_ttc_guard_bad_dir}
                -DTTC_SCAN_CONTEXT=Test-package
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyNoTtcPayload.cmake)
        set_tests_properties(krkrvita-package-rejects-ttc PROPERTIES
            WILL_FAIL TRUE)

        set(KRKRVITA_RETAIL_TEST_GAME "" CACHE PATH
            "Optional real retail game directory for integration tests")
        set(KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT "" CACHE PATH
            "Optional extracted Kirikiroid2 patch snapshot for integration tests")
        if(NOT KRKRVITA_RETAIL_TEST_GAME AND
           EXISTS "/home/shoui/Agents/CodexMax/色情教団/data.xp3")
            set(KRKRVITA_RETAIL_TEST_GAME
                "/home/shoui/Agents/CodexMax/色情教団")
        endif()
        if(NOT KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.cache/patches/0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300/alldata.js")
            set(KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT
                "${CMAKE_CURRENT_SOURCE_DIR}/.cache/patches/0af1bafdb7a031e4c74fd4b320b6a0a1ec03b300")
        endif()
        if(KRKRVITA_RETAIL_TEST_GAME AND
           KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT)
            add_test(NAME krkrvita-retail-sample
                COMMAND krkrvita-tests --retail-sample
                    "${KRKRVITA_RETAIL_TEST_GAME}"
                    "${KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT}")
            set_tests_properties(krkrvita-retail-sample PROPERTIES
                TIMEOUT 300
                LABELS "retail;integration")

            find_program(KRKRVITA_FFMPEG_EXECUTABLE ffmpeg)
            set(KRKRVITA_SAMPLE_FILTER
                "${KRKRVITA_RETAIL_TEST_PATCH_SNAPSHOT}/patch/ORCSOFT／DWARFSOFT/色情教団/xp3filter.tjs")
            if(KRKRVITA_FFMPEG_EXECUTABLE AND
               EXISTS "${KRKRVITA_SAMPLE_FILTER}")
                add_test(NAME krkrvita-retail-audio
                    COMMAND "${CMAKE_COMMAND}"
                        -DTOOL=$<TARGET_FILE:krkrvita-tool>
                        -DGAME_DIR=${KRKRVITA_RETAIL_TEST_GAME}
                        -DFILTER=${KRKRVITA_SAMPLE_FILTER}
                        -DFFMPEG=${KRKRVITA_FFMPEG_EXECUTABLE}
                        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/retail-audio-test
                        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRetailAudio.cmake")
                set_tests_properties(krkrvita-retail-audio PROPERTIES
                    TIMEOUT 120
                    LABELS "retail;integration;audio"
                    RUN_SERIAL TRUE)
            endif()
        endif()
    endif()
endif()
