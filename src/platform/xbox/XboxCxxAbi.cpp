// MSVC C++ ABI support that nxdk does not provide.
//
// nxdk builds the vcruntime scaffolding but supplies none of the dynamic-cast
// machinery: its eh.obj defines only set_terminate/get_terminate, and no nxdk
// library defines __RTDynamicCast. Any nxdk C++ code using dynamic_cast on a
// polymorphic type therefore fails to link. Generated Helengine Core needs it
// for generic implementation dispatch, which picks a concrete generated type
// (for example BinaryReaderLE versus BinaryReaderBE) and cannot be lowered
// without RTTI; asking codegen for an RTTI-free build fails with CPP1001.
//
// clang targeting i386-pc-win32 emits the standard MSVC RTTI structures, so
// this implements the documented lookup against them. Layout reference: the
// complete object locator sits immediately before the vftable, and each base
// class descriptor carries a PMD describing how to reach its subobject.
//
// 32-bit x86 only: on this ABI the RTTI structures hold direct pointers rather
// than the image-relative offsets used by 64-bit targets.

#include "XboxDiagnostics.hpp"

#include <cstring>

namespace {

/// Displacement describing how to reach a base subobject from a complete object.
struct MemberDisplacement {
    int MemberOffset;       // mdisp: offset of the subobject
    int VirtualBaseOffset;  // pdisp: offset to the virtual base table, or negative when not virtual
    int VirtualBaseIndex;   // vdisp: offset inside the virtual base table
};

struct TypeDescriptor {
    const void* VirtualTable;
    void* Spare;
    char Name[1]; // null-terminated decorated name follows
};

struct BaseClassDescriptor {
    const TypeDescriptor* Type;
    unsigned int ContainedBaseCount;
    MemberDisplacement Where;
    unsigned int Attributes;
};

struct BaseClassArray {
    const BaseClassDescriptor* Entries[1];
};

struct ClassHierarchyDescriptor {
    unsigned int Signature;
    unsigned int Attributes;
    unsigned int BaseClassCount;
    const BaseClassArray* BaseClasses;
};

struct CompleteObjectLocator {
    unsigned int Signature;
    unsigned int Offset;            // offset of this vftable's subobject in the complete object
    unsigned int ConstructorOffset; // constructor displacement
    const TypeDescriptor* Type;
    const ClassHierarchyDescriptor* Hierarchy;
};

/// Reads the complete object locator stored immediately before an object's vftable.
const CompleteObjectLocator* LocatorOf(const void* object) {
    const void* const* virtualTable = *reinterpret_cast<const void* const* const*>(object);
    if (virtualTable == nullptr) {
        return nullptr;
    }
    return static_cast<const CompleteObjectLocator*>(*(virtualTable - 1));
}

/// Compares two type descriptors. Pointer equality is the common case within one
/// image; the decorated-name comparison keeps the lookup correct when identical
/// types were emitted into separate translation units and not folded.
bool SameType(const TypeDescriptor* left, const TypeDescriptor* right) {
    if (left == right) {
        return true;
    }
    if (left == nullptr || right == nullptr) {
        return false;
    }
    return std::strcmp(left->Name, right->Name) == 0;
}

/// Applies a base class displacement to reach the base subobject.
void* ApplyDisplacement(void* completeObject, const MemberDisplacement& where) {
    char* address = static_cast<char*>(completeObject);
    if (where.VirtualBaseOffset >= 0) {
        // Virtual base: step to the virtual base table, then read the entry.
        char* virtualBaseTableSlot = address + where.VirtualBaseOffset;
        const char* virtualBaseTable = *reinterpret_cast<char* const*>(virtualBaseTableSlot);
        const int displacement = *reinterpret_cast<const int*>(virtualBaseTable + where.VirtualBaseIndex);
        address = virtualBaseTableSlot + displacement;
    }
    return address + where.MemberOffset;
}

} // namespace

extern "C" {

/// Implements the MSVC ABI dynamic_cast helper.
///
/// \param object Source pointer, already adjusted to the subobject whose static type is sourceType.
/// \param virtualTableDelta Offset from the source pointer back to its vftable pointer.
/// \param sourceType Static type of the source expression. Unused: the lookup starts from the
///        object's own locator, which is authoritative and survives sidecasts.
/// \param targetType Type being cast to.
/// \param isReference Nonzero when the cast came from a reference, where failure cannot return null.
/// \returns Adjusted pointer to the target subobject, or null when the cast does not apply.
void* __RTDynamicCast(
    void* object,
    long virtualTableDelta,
    void* sourceType,
    void* targetType,
    int isReference) {
    (void)sourceType;

    if (object == nullptr) {
        return nullptr; // casting a null pointer yields null
    }

    void* const subobject = static_cast<char*>(object) - virtualTableDelta;
    const CompleteObjectLocator* locator = LocatorOf(subobject);
    if (locator == nullptr || locator->Hierarchy == nullptr || locator->Hierarchy->BaseClasses == nullptr) {
        if (isReference != 0) {
            helengine::xbox::Diagnostics::Log("abi error: dynamic_cast on an object with no usable RTTI");
        }
        return nullptr;
    }

    // Step back from this subobject to the start of the complete object.
    void* const completeObject =
        static_cast<char*>(subobject) - locator->Offset - locator->ConstructorOffset;

    const TypeDescriptor* const wanted = static_cast<const TypeDescriptor*>(targetType);
    const ClassHierarchyDescriptor* const hierarchy = locator->Hierarchy;
    for (unsigned int index = 0; index < hierarchy->BaseClassCount; ++index) {
        const BaseClassDescriptor* const base = hierarchy->BaseClasses->Entries[index];
        if (base == nullptr || !SameType(base->Type, wanted)) {
            continue;
        }
        return ApplyDisplacement(completeObject, base->Where);
    }

    if (isReference != 0) {
        // A failed reference cast must not return null. Exceptions are disabled on
        // this target, so report and stop rather than hand back an invalid reference.
        helengine::xbox::Diagnostics::Log("abi fatal: failed dynamic_cast on a reference");
    }
    return nullptr;
}

} // extern "C"
