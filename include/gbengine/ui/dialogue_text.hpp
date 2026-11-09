#pragma once
#include "gbengine/assets/records.hpp"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace GBEngine {
// Native1709:0768 cursor and word-break contract for ECL11/12's38x6 box.
// Serialized visible text includes cursor whitespace, so schema6 needs no
// additional field at an idle boundary. Intermediate pages remain transient.
class DialogueText {
    std::array<std::string,6> rows_{};
    std::size_t column_=0,row_=0;
    static bool punctuation(char value) {return std::string_view("!,-.:;?").find(value)!=std::string_view::npos;}
    void put(char value) {
        if(row_>=6 || column_>=38)throw DecodeError("Native dialogue token exceeds the owned text rectangle");
        auto& row=rows_[row_];if(row.size()<=column_)row.resize(column_+1,' ');
        row[column_++]=value;
    }
public:
    DialogueText()=default;
    std::pair<std::size_t,std::size_t> cursor()const {return {column_,row_};}
    void cursor(std::size_t x,std::size_t y) {column_=x;row_=y;}
    explicit DialogueText(std::string_view encoded) {
        const bool cursor=encoded.size()>=3 && encoded[encoded.size()-3]=='\x02';
        const auto saved_row=cursor?static_cast<unsigned char>(encoded[encoded.size()-2])-2:0;
        const auto saved_column=cursor?static_cast<unsigned char>(encoded.back())-2:0;
        if(cursor)encoded.remove_suffix(3);
        for(char value:encoded) {
            if(value=='\n') {column_=0;++row_;continue;}
            if(row_>=6 || column_>=38) {rows_={};column_=0;row_=0;break;}
            put(value);
        }
        if(cursor && saved_row<=6 && saved_column<=38){row_=saved_row;column_=saved_column;}
    }
    static std::string_view visible(std::string_view value) {
        if(value.size()>=3 && value[value.size()-3]=='\x02')value.remove_suffix(3);
        return value;
    }
    std::string text() const {
        std::string result;
        std::size_t last=std::min<std::size_t>(row_,6);
        for(std::size_t i=0;i<6;++i)if(!rows_[i].empty())last=std::max(last,i);
        for(std::size_t i=0;i<=last;++i) {
            if(i)result+='\n';
            if(i<6)result+=rows_[i];
        }
        if(row_<last || (row_<6 && column_!=rows_[row_].size())) {
            result+='\x02';result+=static_cast<char>(row_+2);result+=static_cast<char>(column_+2);
        }
        return result;
    }
    std::vector<std::string> append(std::string_view value,bool clear=false,bool newline=false) {
        if(clear){rows_={};column_=0;row_=0;}
        if(newline) {column_=0;++row_;return {text()};}
        // Native validates the cursor once per0768 invocation. Returning past
        // the bottom does not immediately clear or scroll its existing pixels.
        if(row_>=6 || column_>=38){column_=0;row_=0;}
        std::vector<std::string> pages;
        std::size_t first=0;
        while(first<value.size()) {
            std::size_t end=first;
            while(end+1<value.size() && punctuation(value[end]))++end;
            while(end+1<value.size() && !punctuation(value[end]) && value[end]!=' ')++end;
            if(value[end]!=' ')while(end+1<value.size() && punctuation(value[end+1]))++end;
            auto count=end-first+1;
            if(count>38)throw DecodeError("Native dialogue contains an unbounded word token");
            if(column_+count>38) {
                // A boundary space itself is discarded by native0714.
                if(column_+count==39 && value[end]==' ') {
                    for(std::size_t i=first;i<end;++i)put(value[i]);
                    first=end;
                }
                column_=0;++row_;
                while(first<value.size() && value[first]==' ')++first;
                if(first>=value.size())break;
                if(row_>=6) {pages.push_back(text());rows_={};row_=0;}
                continue;
            }
            for(std::size_t i=first;i<=end;++i)put(value[i]);
            first=end+1;
        }
        if(column_>=38){column_=0;++row_;}
        pages.push_back(text());return pages;
    }
};
}
