#include "XboxAbiChecks.hpp"

#include "XboxDiagnostics.hpp"

#include <cstdio>

namespace helengine::xbox {
namespace {

// Single-inheritance chain: exercises the plain member-displacement path.
struct Base {
    virtual ~Base() = default;
    virtual int Id() const { return 1; }
    int BaseField = 0x11;
};

struct Middle : Base {
    int Id() const override { return 2; }
    int MiddleField = 0x22;
};

struct Leaf : Middle {
    int Id() const override { return 3; }
    int LeafField = 0x33;
};

// Unrelated polymorphic type used as a cross-cast target.
struct Side {
    virtual ~Side() = default;
    virtual int Tag() const { return 0x55; }
    int SideField = 0x66;
};

// Multiple inheritance: casting between Base and Side requires walking to the
// complete object and back out to a different subobject, so a naive
// implementation that returns the incoming pointer unchanged fails here.
struct MultiLeaf : Base, Side {
    int Id() const override { return 4; }
    int Tag() const override { return 0x77; }
};

// Virtual inheritance: reaching VirtualBase goes through the virtual base
// table, which is the pdisp/vdisp path in the displacement record.
struct VirtualBase {
    virtual ~VirtualBase() = default;
    virtual int Which() const { return 10; }
    int VirtualField = 0x88;
};

struct VirtualLeft : virtual VirtualBase {
    int Which() const override { return 11; }
};

struct VirtualRight : virtual VirtualBase {
    int Which() const override { return 12; }
};

struct VirtualDiamond : VirtualLeft, VirtualRight {
    int Which() const override { return 13; }
};

void Record(AbiCheckResults& results, bool condition, const char* name) {
    if (condition) {
        ++results.Passed;
        return;
    }
    ++results.Failed;
    if (results.FirstFailure[0] == '\0') {
        std::snprintf(results.FirstFailure, AbiCheckResults::MaxNameLength, "%s", name);
    }
    Diagnostics::Log("abi check failed: %s", name);
}

} // namespace

AbiCheckResults RunDynamicCastChecks() {
    AbiCheckResults results;

    // A null pointer casts to null rather than faulting.
    Base* nullBase = nullptr;
    Record(results, dynamic_cast<Leaf*>(nullBase) == nullptr, "null-stays-null");

    // Downcast to the exact dynamic type.
    Leaf leaf;
    Base* leafAsBase = &leaf;
    Leaf* recoveredLeaf = dynamic_cast<Leaf*>(leafAsBase);
    Record(results, recoveredLeaf == &leaf, "downcast-to-exact-type");
    Record(results, recoveredLeaf != nullptr && recoveredLeaf->LeafField == 0x33, "downcast-preserves-fields");

    // Downcast to an intermediate base.
    Middle* recoveredMiddle = dynamic_cast<Middle*>(leafAsBase);
    Record(results, recoveredMiddle == static_cast<Middle*>(&leaf), "downcast-to-intermediate");

    // A cast to a type the object is not must be rejected.
    Middle middle;
    Base* middleAsBase = &middle;
    Record(results, dynamic_cast<Leaf*>(middleAsBase) == nullptr, "reject-unrelated-downcast");

    // Cross-cast across multiple inheritance: the returned pointer must be the
    // Side subobject, which sits at a different address than the Base one.
    MultiLeaf multi;
    Base* multiAsBase = &multi;
    Side* crossed = dynamic_cast<Side*>(multiAsBase);
    Record(results, crossed == static_cast<Side*>(&multi), "cross-cast-to-sibling");
    Record(results, crossed != nullptr && crossed->Tag() == 0x77, "cross-cast-dispatches-correctly");
    Record(results, static_cast<void*>(crossed) != static_cast<void*>(multiAsBase), "cross-cast-adjusts-pointer");

    // Recover the most derived type from the sibling subobject.
    Record(results, dynamic_cast<MultiLeaf*>(crossed) == &multi, "cross-cast-back-to-derived");

    // Virtual base: reaching the derived object from a virtual base pointer
    // exercises the virtual base table displacement.
    VirtualDiamond diamond;
    VirtualBase* diamondAsVirtual = &diamond;
    Record(results, dynamic_cast<VirtualDiamond*>(diamondAsVirtual) == &diamond, "virtual-base-downcast");
    VirtualLeft* leftArm = dynamic_cast<VirtualLeft*>(diamondAsVirtual);
    Record(results, leftArm == static_cast<VirtualLeft*>(&diamond), "virtual-base-to-branch");
    Record(results, diamondAsVirtual->Which() == 13, "virtual-base-dispatches-correctly");

    Diagnostics::Log("abi checks: %u passed, %u failed", results.Passed, results.Failed);
    return results;
}

} // namespace helengine::xbox
