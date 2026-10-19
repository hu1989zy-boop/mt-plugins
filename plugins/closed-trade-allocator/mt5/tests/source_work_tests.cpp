#include "source_work.hpp"
#include <iostream>
#include <stdexcept>
using closedalloc::mt5::SourceWork;
void expect(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
int main() {
  try {
    SourceWork work(2);
    work.touch(10,1000);work.touch(10,1001);
    expect(work.size()==1&&work.take(1000)==10,"events did not coalesce");
    expect(!work.stable(10,"snapshot",1000,0,5000),"first observation accepted");
    expect(work.take(5999)==0&&work.take(6000)==10,"stability deadline added another poll interval");
    expect(work.stable(10,"snapshot",6000,5000,5000),"unchanged close not ready after one stability window");
    work.erase(10);
    work.touch(20,10000);expect(work.take(10000)==20,"initial work missing");
    expect(!work.stable(20,"old",10000,20000,5000),"old close bypassed second snapshot");
    work.touch(20,11000);expect(work.take(11000)==20,"update did not advance read");
    expect(!work.stable(20,"new",11000,21000,5000),"source revision accepted");
    work.touch(20,12000);expect(work.take(12000)==20,"duplicate event missing");
    expect(!work.stable(20,"new",12000,22000,5000),"duplicate event skipped window");
    expect(work.take(15999)==0&&work.take(16000)==20,"duplicate event extended stability window");
    expect(work.stable(20,"new",16000,26000,5000),"revised stable snapshot rejected");
    work.erase(20);
    work.touch(30,0);work.take(0);
    expect(!work.stable(30,"future close",0,-2000,5000),"future close accepted");
    expect(work.take(6999)==0&&work.take(7000)==30,"close age was not enforced");
    work.defer(30,9000);work.touch(40,8000);
    expect(work.take(8000)==40&&work.take(9000)==30,"due order is not fair");
    bool bounded=false;try{work.touch(50,9000);}catch(const std::exception&){bounded=true;}
    expect(bounded,"unbounded pending work");work.erase(40);work.touch(50,9000);
    expect(work.take(9000)==50,"capacity not reusable");
    std::cout<<"Source event scheduling tests passed\n";return 0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
