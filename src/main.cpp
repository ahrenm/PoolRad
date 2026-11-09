#include "gbengine/app/app.hpp"
#include <iostream>
int poolrad_main(int argc,char** argv) {
    try {
        GBEngine::App app(argc,argv);
        return app.run();
    } catch(const std::exception& error) {
        std::cerr<<"PoolRad: "<<error.what()<<'\n';return 1;
    }
}
#ifndef _WIN32
int main(int argc,char** argv) {return poolrad_main(argc,argv);}
#endif
