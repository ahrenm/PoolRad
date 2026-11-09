#pragma once
#include "gbengine/ui/dialogue_text.hpp"
#include "gbengine/rendering/presentation.hpp"
#include "gbengine/rendering/screen_chrome.hpp"
#include <algorithm>

namespace GBEngine {
// Encodes resident shared cursor/84DE/84DF/84E2 and owned glyph pixels in the
// existing schema6 glyph string. All bytes are nonzero; no schema is extended.
class GlyphText {
public:
    struct Cell {std::uint8_t x,y,glyph;};
    std::vector<Cell> cells;
    std::string legacy;
    std::uint8_t x=1,y=17;
    bool cleared=false,frame_pending=false,frame_restored=false,interior_frame=false,glyph_mode=false,canvas=false;
    bool native=false;
    explicit GlyphText(std::string_view encoded={}) {
        if(!encoded.starts_with("\x03GT1")) {legacy=encoded;return;}
        if(encoded.size()<9 || (encoded.size()-9)%3)throw DecodeError("Malformed owned glyph presentation");
        native=true;
        const auto read=[&](std::size_t i){const auto v=static_cast<std::uint8_t>(encoded[i]);if(!v)throw DecodeError("Invalid zero glyph presentation byte");return static_cast<unsigned>(v-1);};
        const auto flags=read(4);if(flags>63)throw DecodeError("Invalid glyph presentation flags");
        for(std::size_t i=5;i<9;++i)if(read(i)>15)throw DecodeError("Invalid glyph cursor nibble");
        x=static_cast<std::uint8_t>(16*read(5)+read(6));y=static_cast<std::uint8_t>(16*read(7)+read(8));
        cleared=flags&1;frame_pending=flags&2;frame_restored=flags&4;interior_frame=flags&8;glyph_mode=flags&16;
        canvas=flags&32;
        for(std::size_t i=9;i<encoded.size();i+=3) {
            const auto cx=read(i),cy=read(i+1),glyph=read(i+2);
            if(cx>=40 || cy>=25 || glyph>=GameFont::glyph_count)throw DecodeError("Invalid owned glyph cell");
            cells.push_back({static_cast<std::uint8_t>(cx),static_cast<std::uint8_t>(cy),static_cast<std::uint8_t>(glyph)});
        }
        if(cells.size()>1000)throw DecodeError("Too many owned glyph cells");
    }
    bool has_art()const {return !legacy.empty() || canvas || frame_restored || !cells.empty();}
    std::string encode()const {
        if(!native)return legacy;
        std::string value="\x03GT1";
        value+=static_cast<char>(1+(cleared?1:0)+(frame_pending?2:0)+(frame_restored?4:0)+(interior_frame?8:0)+(glyph_mode?16:0)+(canvas?32:0));
        value+=static_cast<char>(1+(x>>4));value+=static_cast<char>(1+(x&15));value+=static_cast<char>(1+(y>>4));value+=static_cast<char>(1+(y&15));
        for(const auto cell:cells){value+=static_cast<char>(cell.x+1);value+=static_cast<char>(cell.y+1);value+=static_cast<char>(cell.glyph+1);}
        return value;
    }
    void adopt_cursor(std::string_view dialogue) {
        if(native)return;
        const DialogueText text(dialogue);const auto cursor=text.cursor();
        x=static_cast<std::uint8_t>(cursor.first+1);y=static_cast<std::uint8_t>(cursor.second+17);
        native=true;
    }
    void clear_box(std::string_view dialogue) {
        adopt_cursor(dialogue);legacy.clear();cells.clear();cleared=true;canvas=true;frame_restored=false;
    }
    void clear_dialogue() {
        // PRINTCLEAR owns cells1..38,17..22; viewport art and its frame survive.
        std::erase_if(cells,[](const Cell& cell){return cell.x>=1 && cell.x<=38 && cell.y>=17 && cell.y<=22;});
    }
    void newline(std::string_view dialogue) {
        adopt_cursor(dialogue);
        x=1;++y;glyph_mode=false;
    }
    void append(std::span<const std::uint16_t> glyphs,std::string_view dialogue) {
        adopt_cursor(dialogue);
        if(!legacy.empty())throw DecodeError("Legacy glyph pixels require a source CLEAR_BOX before native table continuation");
        if(cleared && y>13){x=2;y=5;frame_pending=true;frame_restored=false;}
        glyph_mode=true;
        for(const auto glyph:glyphs) {
            if(glyph>=GameFont::glyph_count)throw DecodeError("Native glyph table references an unavailable font bitmap");
            if(x>=40 || y>=25){++x;continue;} // Native1709:01DF's screen veto.
            const auto at=std::find_if(cells.begin(),cells.end(),[&](const auto& cell){return cell.x==x && cell.y==y;});
            const Cell next{x,y,static_cast<std::uint8_t>(glyph)};
            if(at==cells.end())cells.push_back(next);else *at=next;
            ++x;
        }
        ++y;
    }
    void dialogue_cursor(std::pair<std::size_t,std::size_t> cursor) {
        native=true;x=static_cast<std::uint8_t>(cursor.first+1);y=static_cast<std::uint8_t>(cursor.second+17);glyph_mode=false;
    }
    void exit(bool interior) {
        if(!native)return;
        if(frame_pending){cells.clear();legacy.clear();cleared=false;canvas=true;frame_restored=true;interior_frame=interior;}
        frame_pending=false;cleared=false;x=1;y=17;
    }
    void draw_cells(Framebuffer& frame,const GameFont& font)const {
        for(const auto cell:cells) {
            const int left=cell.x*8,top=cell.y*8;
            for(int dy=0;dy<8;++dy)for(int dx=0;dx<8;++dx)frame[static_cast<std::size_t>((top+dy)*320+left+dx)]=ega_ui_palette[0];
            font.draw_glyph(frame,cell.glyph,left,top,ega_ui_palette[15]);
        }
    }
};
}
