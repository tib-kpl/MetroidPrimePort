#ifndef _CTEXTRENDERBUFFER
#define _CTEXTRENDERBUFFER

#include "Kyoto/Graphics/CGraphicsPalette.hpp"
#include "Kyoto/Math/CVector2i.hpp"
#include "Kyoto/TToken.hpp"
#include "Kyoto/Text/CFontImageDef.hpp"
#include "Kyoto/Text/CTextColor.hpp"

#include "rstl/pair.hpp"
#include <rstl/reserved_vector.hpp>
#include <rstl/vector.hpp>
#ifdef TARGET_PC
#include <memory>
#include <vector>
#endif
class CColor;
class CRasterFont;

// The European release's buffer keeps four palettes per font change, in code
// that is not decompiled for it; the PC build of it uses the USA buffer.
#if VERSION >= VERSION_GM8P_00 && VERSION != VERSION_GM8E_02 && !defined(TARGET_PC)
#define TEXT_RENDER_BUFFER_PAL 1
#else
#define TEXT_RENDER_BUFFER_PAL 0
#endif

class CTextRenderBuffer {
public:
  enum ECmd {
    kC_CharacterRender,
    kC_ImageRender,
    kC_FontChange,
    kC_PaletteChange,
    kC_Invalid = -1,
  };
  enum EMode {
    kM_AllocTally,
    kM_BufferFill,
  };

  struct Primitive {
    Primitive(ECmd cmd, short x, short y, short chr, uint color, schar index)
    : x0_color(color), x4_cmd(cmd), x8_x(x), xa_y(y), xc_char(chr), xe_index(index) {}

    uint x0_color;
    ECmd x4_cmd;
    short x8_x;
    short xa_y;
    short xc_char;
    schar xe_index;
  };

#if TEXT_RENDER_BUFFER_PAL
  struct SFontPalette {
    int x0_;
    uint x4_;
    uint x8_;
    rstl::auto_ptr< CGraphicsPalette > xc_palette;
    rstl::auto_ptr< CGraphicsPalette > x14_palette;
    rstl::auto_ptr< CGraphicsPalette > x1c_palette;
    rstl::auto_ptr< CGraphicsPalette > x24_palette;
  };
#endif

  CTextRenderBuffer(EMode mode);

  CGraphicsPalette* GetNextAvailablePalette() const;
  int GetMatchingPaletteIndex(const CGraphicsPalette& palette) const;
  void AddFontChange(const TToken< CRasterFont >& font);
  void AddPaletteChange(const CGraphicsPalette& palette);
  void AddCharacter(const CVector2i&, short chr, uint color);
  void AddImage(const CVector2i& offset, const CFontImageDef& image);

  void* GetOutStream();
  size_t GetCurLen();
  void SetMode(EMode mode);
  void Render(const CColor& color, float time) const;
  int GetNumPrimitives() const { return x24_primOffsets.size(); }
  Primitive GetPrimitive(int index) const;
  void SetPrimitive(const Primitive& prim, int index);
  rstl::pair< CVector2i, CVector2i > AccumulateTextBounds();
  bool HasSpaceAvailable(const CVector2i& origin, const CVector2i& extent);

private:
  void VerifyBuffer();
#ifdef TARGET_PC
  const CGraphicsPalette& PortLayerPalette(int palette, int layer, int mode) const;
#endif

  EMode x0_mode;
  rstl::vector< TToken< CRasterFont > > x4_fonts;
  rstl::vector< CFontImageDef > x14_images;
  rstl::vector< int > x24_primOffsets;
  rstl::vector< signed char > x34_bytecode;
  uint x44_blobSize;
  uint x48_curBytecodeOffset;
  mutable char x4c_activeFont;
  mutable char x4d_activePalette;
  mutable char x4e_queuedFont;
  mutable char x4f_queuedPalette;
#if TEXT_RENDER_BUFFER_PAL
  mutable rstl::reserved_vector< SFontPalette, 64 > x50_palettes;
#else
  mutable rstl::reserved_vector< rstl::auto_ptr< CGraphicsPalette >, 64 > x50_palettes;
#endif
  mutable int x254_nextPalette;
#ifdef TARGET_PC
  // A font of layers (the European release's) keeps several glyphs in each
  // texel; per palette, the copy that reads each layer's bits, made on first
  // use. Four per palette, shared so the buffer stays copyable.
  mutable std::vector< std::shared_ptr< CGraphicsPalette > > xPortLayerPalettes;
#endif
#if TEXT_RENDER_BUFFER_PAL
  CVector2i xb58_;
  CVector2i xb60_;
  bool xb68_;
#endif
};

CHECK_SIZEOF(CTextRenderBuffer, (TEXT_RENDER_BUFFER_PAL ? 0xb6c : 0x258))
NESTED_CHECK_SIZEOF(CTextRenderBuffer, Primitive, 0x10)
#if TEXT_RENDER_BUFFER_PAL
NESTED_CHECK_SIZEOF(CTextRenderBuffer, SFontPalette, 0x2c)
#endif

#endif // _CTEXTRENDERBUFFER
