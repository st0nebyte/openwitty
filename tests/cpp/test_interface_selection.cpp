// Phase 7A: descriptor-driven control-interface selection. The positive fixture is the exact
// descriptor layout recorded from a real CONNECT 2 (docs/HARDWARE_TEST_RESULTS.md §2); every
// other table is synthetic. Selection itself is pure logic - nothing here touches USB.
#include <gtest/gtest.h>

#include "transport/InterfaceSelection.hpp"
#include "transport/UsbTransport.hpp"

using namespace lewitt;

namespace {
InterfaceDescriptor ifc(int n, int k, int s, int p, int ep, bool known, bool bound) {
    InterfaceDescriptor d;
    d.number = n;
    d.klass = static_cast<std::uint8_t>(k);
    d.subclass = static_cast<std::uint8_t>(s);
    d.protocol = static_cast<std::uint8_t>(p);
    d.numEndpoints = ep;
    d.driverKnown = known;
    d.driverBound = bound;
    return d;
}
// Real CONNECT 2 (29c2:0004, bcdDevice 0121), altsetting 0 of each interface.
std::vector<InterfaceDescriptor> realConnect2() {
    return {
        ifc(0, 0x01, 0x01, 0x20, 1, true, true),    // UAC2 AudioControl, snd-usb-audio
        ifc(1, 0x01, 0x02, 0x20, 0, true, true),    // AudioStreaming (alt 0 has no endpoints)
        ifc(2, 0x01, 0x02, 0x20, 0, true, true),
        ifc(3, 0xFF, 0xF0, 0x00, 0, true, false),   // vendor-specific control, unbound
        ifc(4, 0xFE, 0x01, 0x01, 0, true, false),   // DFU
    };
}
}  // namespace

TEST(InterfaceSelection, RealConnect2SelectsVendorInterface3) {
    auto r = selectControlInterface(0x29C2, 0x0004, realConnect2());
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(r.value(), 3);
}

TEST(InterfaceSelection, MacOSStreamingModePidIsAlsoAccepted) {
    auto r = selectControlInterface(0x29C2, 0x0002, realConnect2());
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 3);
}

TEST(InterfaceSelection, OrderAndNumberingDoNotMatter) {
    // The control interface is found by its descriptor properties, not by "3" or by position.
    std::vector<InterfaceDescriptor> v = {
        ifc(7, 0xFF, 0xF0, 0, 0, true, false), ifc(5, 0xFE, 1, 1, 0, true, false), ifc(1, 1, 1, 0x20, 1, true, true)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.value(), 7);
}

TEST(InterfaceSelection, AudioControlIsNeverChosenEvenIfFirstOrOnlyOne) {
    std::vector<InterfaceDescriptor> v = {ifc(0, 0x01, 0x01, 0x20, 1, true, false)};   // even if unbound
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotFound);
}

TEST(InterfaceSelection, DfuInterfaceIsRejectedAndNeverSelected) {
    std::vector<InterfaceDescriptor> v = {ifc(4, 0xFE, 0x01, 0x01, 0, true, false)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("DFU"), std::string::npos) << r.error().message;
    // a DFU-looking interface that ALSO claims the vendor class signature is still not selected
    // because class 0xFE is what identifies it; and 0xFE with subclass 0xF0 is rejected too
    std::vector<InterfaceDescriptor> w = {ifc(4, 0xFE, 0xF0, 0x00, 0, true, false)};
    EXPECT_FALSE(selectControlInterface(0x29C2, 0x0004, w).ok());
}

TEST(InterfaceSelection, KernelBoundVendorInterfaceFailsClosedAsBusy) {
    std::vector<InterfaceDescriptor> v = {ifc(3, 0xFF, 0xF0, 0, 0, true, /*bound=*/true)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::Busy);
}

TEST(InterfaceSelection, UnknownDriverStateFailsClosed) {
    std::vector<InterfaceDescriptor> v = {ifc(3, 0xFF, 0xF0, 0, 0, /*known=*/false, false)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::Busy);
}

TEST(InterfaceSelection, MissingVendorInterfaceReportsWhatWasSeen) {
    std::vector<InterfaceDescriptor> v = {ifc(0, 1, 1, 0x20, 1, true, true), ifc(1, 1, 2, 0x20, 0, true, true)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotFound);
    EXPECT_NE(r.error().message.find("interface 0"), std::string::npos);
}

TEST(InterfaceSelection, EmptyInterfaceListFailsClosed) {
    auto r = selectControlInterface(0x29C2, 0x0004, std::vector<InterfaceDescriptor>{});
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::NotFound);
}

TEST(InterfaceSelection, AmbiguousVendorInterfacesAreNotGuessed) {
    std::vector<InterfaceDescriptor> v = {ifc(3, 0xFF, 0xF0, 0, 0, true, false), ifc(6, 0xFF, 0xF0, 0, 0, true, false)};
    auto r = selectControlInterface(0x29C2, 0x0004, v);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, Errc::Malformed);
    EXPECT_NE(r.error().message.find("ambiguous"), std::string::npos);
}

TEST(InterfaceSelection, GenericVendorSpecificInterfaceIsNotAssumedSafe) {
    // class ff but a different subclass, or with endpoints: not the documented signature
    std::vector<InterfaceDescriptor> a = {ifc(3, 0xFF, 0x00, 0, 0, true, false)};
    std::vector<InterfaceDescriptor> b = {ifc(3, 0xFF, 0xF0, 0, 2, true, false)};
    EXPECT_FALSE(selectControlInterface(0x29C2, 0x0004, a).ok());
    EXPECT_FALSE(selectControlInterface(0x29C2, 0x0004, b).ok());
}

TEST(InterfaceSelection, UnsupportedVidPidFailClosedBeforeLookingAtInterfaces) {
    auto other = selectControlInterface(0x1234, 0x0004, realConnect2());
    ASSERT_FALSE(other.ok());
    EXPECT_EQ(other.error().code, Errc::NotSupported);
    auto unknownPid = selectControlInterface(0x29C2, 0x0123, realConnect2());
    ASSERT_FALSE(unknownPid.ok());
    EXPECT_EQ(unknownPid.error().code, Errc::NotSupported);
    auto boot = selectControlInterface(0x29C2, 0x000A, realConnect2());   // probable bootloader
    ASSERT_FALSE(boot.ok());
    EXPECT_EQ(boot.error().code, Errc::NotSupported);
    EXPECT_NE(boot.error().message.find("bootloader"), std::string::npos);
}

TEST(InterfaceSelection, ExplicitNumberPointingAtDfuIsRefused) {
    auto v = realConnect2();
    EXPECT_FALSE(checkExplicitInterface(4, v).ok());
    EXPECT_TRUE(checkExplicitInterface(3, v).ok());
    EXPECT_TRUE(checkExplicitInterface(0, v).ok());      // experiments on other interfaces stay possible
    EXPECT_TRUE(checkExplicitInterface(9, v).ok());      // unknown number: caller's responsibility
}

TEST(InterfaceSelection, GuiTransportOptionsAreReadOnlyAndNonDisruptive) {
    UsbOptions o = readOnlyHardwareOptions();
    EXPECT_FALSE(o.allowWrites);
    EXPECT_FALSE(o.allowDisruptive);
    EXPECT_FALSE(o.allowDetachKernelDriver);
    EXPECT_EQ(o.method, AccessMethod::InterfaceRecipient);
    EXPECT_EQ(o.interfaceStrategy, InterfaceStrategy::VendorControlAuto);
    EXPECT_EQ(o.interfaceNumber, -1) << "the number must come from the descriptors, not be hard-coded";
    EXPECT_EQ(o.request, MemRequest::Uac2Mem);
}

TEST(InterfaceSelection, LegacyDefaultsAreUnchanged) {
    UsbOptions o;   // what existing CLI read paths construct
    EXPECT_EQ(o.interfaceStrategy, InterfaceStrategy::AudioControl);
    EXPECT_EQ(o.interfaceNumber, -1);
    EXPECT_FALSE(o.allowWrites);
}
