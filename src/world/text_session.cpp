#include "gbengine/world/exploration_session.hpp"
#include "gbengine/ui/dialogue_text.hpp"
#include "gbengine/ui/glyph_text.hpp"

namespace GBEngine {
bool ExplorationSession::stage_text(const EclRequest& request) {
    if(text_pages_) {
        if(text_pages_->token!=request.token)throw DecodeError("Stale dialogue page owner");
        return false;
    }
    // Preserve an unread source response before a later PRINTCLEAR replaces it.
    // The new request remains unconsumed: acknowledgement stages it normally,
    // including its genuine Journal completion, rather than skipping its text.
    if(pause_text_replacement_ && request.opcode==0x12 &&
       acknowledged_dialogue_revision_!=dialogue_revision_ &&
       DialogueText::visible(display_text).find_first_not_of(" \r\n")!=std::string_view::npos) {
        text_pages_=TextPages{request.token,{display_text},0,false,true};return false;
    }
    DialogueText layout(display_text);
    GlyphText glyphs(glyph_text_);
    if(request.opcode==0x33 && (glyphs.native || !glyphs.legacy.empty())) {
        if(!glyphs.native && !glyphs.legacy.empty())
            throw DecodeError("Legacy symbol cursor unavailable; source CLEAR_BOX required");
        glyphs.newline(display_text);glyph_text_=glyphs.encode();
        if(messages.size()==256)messages.erase(messages.begin());
        messages.push_back(request.text);
        return true;
    }
    if(glyphs.native) {
        if(glyphs.x>=1 && glyphs.x<=38 && glyphs.y>=17 && glyphs.y<=22)layout.cursor(glyphs.x-1,glyphs.y-17);
        else layout.cursor(0,0);
    }
    auto pages=layout.append(request.text,request.opcode==0x12,request.opcode==0x33);
    if(request.opcode==0x12)glyphs.clear_dialogue();
    if(glyphs.native || glyphs.legacy.empty()) {
        const auto prior_glyph_mode=glyphs.glyph_mode;
        glyphs.dialogue_cursor(layout.cursor());glyphs.glyph_mode=prior_glyph_mode;
        glyph_text_=glyphs.encode();
    }
    if(messages.size()==256)messages.erase(messages.begin());
    messages.push_back(request.text);
    display_text=pages.front();++dialogue_revision_;
    if(pages.size()>1) {
        text_pages_=TextPages{request.token,std::move(pages),0,false};return false;
    }
    return true;
}
bool ExplorationSession::showing_glyphs()const {return GlyphText(glyph_text_).has_art();}
void ExplorationSession::text_page_presented(bool wait_for_final_input) {
    if(!text_pages_)return;
    text_pages_->presented=true;
    if(!text_page_has_more() && !wait_for_final_input) {
        const auto token=text_pages_->token;
        if(!text_pages_->replacement)complete_text_request(token);
        acknowledge_dialogue();text_pages_.reset();pump();
    }
}
bool ExplorationSession::text_page_input(InputEvent event,std::size_t budget) {
    if(!text_pages_)return false;
    if(!event.pressed || event.key!=Key::accept || !text_pages_->presented)return true;
    if(!budget || budget>1000000)throw DecodeError("Invalid dialogue page execution budget");
    const auto& request=vm.pending_request();
    if(!request || request->kind!=EclRequestKind::print || request->token!=text_pages_->token)
        throw DecodeError("Dialogue continuation changed before acknowledgement");
    auto& pages=*text_pages_;
    if(text_page_has_more()) {
        ++pages.page;display_text=pages.pages.at(pages.page);++dialogue_revision_;pages.presented=false;
    } else {
        const auto token=pages.token;
        if(!pages.replacement)complete_text_request(token);
        acknowledge_dialogue();text_pages_.reset();pump(budget);
    }
    return true;
}
}
