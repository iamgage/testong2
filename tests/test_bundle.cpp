#include "../shared/saber_bundle.hpp"
#include <cassert>
#include <fstream>
int main() {
    auto a=sfq::inspectBundle("missing.saber"); assert(!a.unityFS);
    std::ofstream f("test_fixture.saber",std::ios::binary);
    const char head[]={ 'U','n','i','t','y','F','S',0, 0,0,0,6, '5','.','x','.','x',0, '2','0','1','8','.','1','.','6','f','1',0 };
    f.write(head,sizeof(head)); for(int i=0;i<40;++i) f.put(0); f.close();
    auto b=sfq::inspectBundle("test_fixture.saber"); assert(b.unityFS && b.unityVersion=="2018.1.6f1");
    std::remove("test_fixture.saber");
}
