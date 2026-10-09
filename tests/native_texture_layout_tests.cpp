#include "platform/xbox/XboxNativeTexturePayload.hpp"
#include "platform/xbox/XboxTextureStageState.hpp"
#include "platform/xbox/XboxShaderConstants.hpp"
#include <pbkit/nv_regs.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

using namespace helengine::xbox;
namespace {
// Independent SDK macros ensure the runtime's complete format table does not omit hardware enum values.
constexpr unsigned Codes[] = {
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_Y8, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_AY8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A1R5G5B5, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X1R5G5B5,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A4R4G4B4, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R5G6B5,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8R8G8B8, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_X8R8G8B8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_I8_A8R8G8B8, NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT1_A1R5G5B5,
    NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT23_A8R8G8B8, NV097_SET_TEXTURE_FORMAT_COLOR_L_DXT45_A8R8G8B8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A1R5G5B5, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R5G6B5,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8B8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_G8B8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8Y8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_AY8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X1R5G5B5,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A4R4G4B4, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_X8R8G8B8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8Y8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_CR8YB8CB8YA8, NV097_SET_TEXTURE_FORMAT_COLOR_LC_IMAGE_YB8CR8YA8CB8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R6G5B5, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_G8B8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8B8, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_DEPTH_Y16_FIXED,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_X8_Y24_FIXED, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FIXED,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_DEPTH_Y16_FLOAT, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_Y16,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_A8B8G8R8, NV097_SET_TEXTURE_FORMAT_COLOR_SZ_B8G8R8A8,
    NV097_SET_TEXTURE_FORMAT_COLOR_SZ_R8G8B8A8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8B8G8R8,
    NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_B8G8R8A8, NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_R8G8B8A8
};
static_assert(sizeof(Codes)/sizeof(Codes[0]) == XboxNativeFormatCount);

// Creates explicit little-endian fixture header words without depending on the production parser.
void Word(std::vector<std::uint8_t>& bytes,unsigned offset,unsigned value) {
    for (unsigned index=0;index<4;++index) bytes[offset+index]=static_cast<std::uint8_t>(value>>(index*8));
}
// Constructs a header around arbitrary native texels so tests can corrupt one field at a time.
std::vector<std::uint8_t> Fixture(unsigned code,unsigned sw,unsigned sh,unsigned pitch,unsigned texels,unsigned entries=0) {
    std::vector<std::uint8_t> bytes(48+texels);
    Word(bytes,0,0x31585458); Word(bytes,4,1); Word(bytes,8,code);
    Word(bytes,16,3); Word(bytes,20,5); Word(bytes,24,sw); Word(bytes,28,sh);
    Word(bytes,32,pitch); Word(bytes,36,texels); Word(bytes,40,entries); Word(bytes,44,entries*4);
    return bytes;
}
// Captures hardware writes, including the final cleared state after a paletted-to-untextured transition.
void Capture(void* context,std::uint32_t method,std::uint32_t value) {
    (*static_cast<std::map<unsigned,unsigned>*>(context))[method]=value;
}

// All 42 formats are reachable; independent expected sizes distinguish linear, Morton, P8 and DXT storage.
void AllFormatsAndLayout() {
    unsigned count=0;
    for (unsigned code:Codes) {
        XboxNativeFormatInfo info;
        assert(TryGetXboxNativeFormatInfo(code,info));
        XboxTextureLayout layout;
        const unsigned entries=code==0x0B?256:0;
        const unsigned bytes=info.Linear?320:info.BlockBytes?info.BlockBytes*2:info.BytesPerTexel*32;
        assert(BuildXboxNativeTextureLayout(3,5,info.Linear?3:4,info.Linear?5:8,info.Linear?64:0,code,entries,layout)==XboxTextureStatus::Ok);
        assert(layout.ByteCount==bytes && ValidateXboxTextureLayout(layout));
        const unsigned expectedFormat=0x1002A|(code<<8)|(info.Linear?0:0x03200000);
        assert(GetXboxTextureFormatWord(layout)==expectedFormat);
        assert(GetXboxTextureUvScaleX(layout)==(info.Linear?3.0f:0.75f));
        assert(GetXboxTextureUvScaleY(layout)==(info.Linear?5.0f:0.625f));
        XboxTextureStageState state;
        assert(BuildTextureStageState(&layout,0x1000,state,entries?0x10000:0));
        assert(state.FormatWord==expectedFormat && state.Pitch==(info.Linear?64u:0u));
        assert(state.Width==3 && state.Height==5);
        ++count;
    }
    assert(count==42);
    XboxNativeFormatInfo info;
    assert(!TryGetXboxNativeFormatInfo(0x2F,info));
    assert(!TryGetXboxNativeFormatInfo(0xFF,info));
    XboxTextureLayout bad;
    assert(BuildXboxNativeTextureLayout(3,5,3,5,12,0x12,0,bad)==XboxTextureStatus::MalformedPayload);
    assert(BuildXboxNativeTextureLayout(3,5,3,8,0,0x06,0,bad)==XboxTextureStatus::MalformedPayload);
    assert(BuildXboxNativeTextureLayout(3,5,4,8,64,0x06,0,bad)==XboxTextureStatus::MalformedPayload);
    assert(BuildXboxNativeTextureLayout(1,1,2,2,0,0x0C,0,bad)==XboxTextureStatus::UnsupportedExtent);
    assert(BuildXboxNativeTextureLayout(4097,1,4097,1,16448,0x12,0,bad)==XboxTextureStatus::UnsupportedExtent);
    assert(BuildXboxNativeTextureLayout(3,5,4,8,0,0x0B,16,bad)==XboxTextureStatus::InvalidPalette);
}

// Payload descriptors require exact header/pixel/palette lengths and indices valid even in padded Morton storage.
void HeaderAndPaletteValidation() {
    auto bytes=Fixture(0x12,3,5,64,320);
    XboxTextureSourceView view{3,5,XboxTextureColorFormat::XboxNative,XboxTextureAlphaPrecision::A8,
        bytes.data(),bytes.size(),nullptr,0};
    XboxNativeTexturePayload payload;
    assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::Ok);
    assert(payload.Texels==bytes.data()+48 && payload.Layout.ByteCount==320);
    for (unsigned offset:{0u,4u,12u,16u,20u,24u,28u,32u,36u,40u,44u}) {
        auto corrupt=bytes;
        Word(corrupt,offset,0xFFFF);
        view.Colors=corrupt.data();
        assert(ParseXboxNativeTexturePayload(view,payload)!=XboxTextureStatus::Ok);
        assert(payload.Texels==nullptr && payload.Layout.ByteCount==0);
    }
    view.Colors=bytes.data(); view.ColorsLength=47;
    assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::MalformedPayload);
    view.ColorsLength=bytes.size()-1;
    assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::MalformedPayload);
    view.ColorsLength=bytes.size()+1;
    assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::MalformedPayload);
    for (unsigned entries:{32u,64u,128u,256u}) {
        auto indexed=Fixture(0x0B,4,8,0,32,entries);
        std::vector<std::uint8_t> palette(entries*4);
        indexed.back()=static_cast<std::uint8_t>(entries-1);
        view={3,5,XboxTextureColorFormat::XboxNative,XboxTextureAlphaPrecision::A8,
            indexed.data(),indexed.size(),palette.data(),palette.size()};
        assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::Ok);
        assert(payload.PaletteByteCount==entries*4);
        if (entries<256) {
            indexed.back()=static_cast<std::uint8_t>(entries);
            assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::InvalidPalette);
        }
        view.PaletteLength--;
        assert(ParseXboxNativeTexturePayload(view,payload)==XboxTextureStatus::InvalidPalette);
    }
}

// P8 binds DMA B and a 64-byte palette base, then every later layout or disabled stage clears stale palette state.
void PaletteBindingsAndUvConstants() {
    XboxTextureLayout palette;
    assert(BuildXboxNativeTextureLayout(3,5,4,8,0,0x0B,32,palette)==XboxTextureStatus::Ok);
    XboxTextureStageState state;
    assert(!BuildTextureStageState(&palette,0x1000,state,0));
    assert(!BuildTextureStageState(&palette,0x1000,state,0x10001));
    assert(!BuildTextureStageState(&palette,0x1000,state,0x03FFAFC0));
    assert(BuildTextureStageState(&palette,0x1000,state,0x10000));
    assert(state.PaletteWord==(0x10000u|13u));
    std::map<unsigned,unsigned> writes;
    assert(BindTextureStage(&palette,0x1000,Capture,&writes,0x10000));
    assert(writes[NV097_SET_TEXTURE_PALETTE]==(0x10000u|13u));
    XboxTextureLayout linear;
    assert(BuildXboxNativeTextureLayout(3,5,3,5,64,0x12,0,linear)==XboxTextureStatus::Ok);
    assert(BindTextureStage(&linear,0x2000,Capture,&writes));
    for (unsigned stage=0;stage<4;++stage) assert(writes[NV097_SET_TEXTURE_PALETTE+stage*64]==0);
    assert(writes[NV097_SET_TEXTURE_CONTROL1]==64u<<16);
    assert(BindTextureStage(&palette,0x1000,Capture,&writes,0x10000));
    assert(BindTextureStage(nullptr,0,Capture,&writes));
    for (unsigned stage=0;stage<4;++stage) {
        assert(writes[NV097_SET_TEXTURE_CONTROL0+stage*64]==0);
        assert(writes[NV097_SET_TEXTURE_PALETTE+stage*64]==0);
    }
    float constants[XboxShaderConstantCount][4];
    Matrix4 clip{};
    assert(BuildShaderDrawConstants(clip,-1,640,480,0.75f,0.625f,constants));
    assert(constants[XboxTextureExtentConstant][0]==0.75f && constants[XboxTextureExtentConstant][1]==0.625f);
    assert(constants[XboxViewportScaleConstant][0]==320.0f && constants[XboxViewportScaleConstant][1]==-240.0f);
    assert(!BuildShaderDrawConstants(clip,-1,640,480,NAN,0.625f,constants));
}
}

int main() {
    AllFormatsAndLayout();
    HeaderAndPaletteValidation();
    PaletteBindingsAndUvConstants();
    std::puts("42 Xbox native formats: layout, header, palette, register and UV tests passed");
}
