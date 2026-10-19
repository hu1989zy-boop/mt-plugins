#include "dealer_policy.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
unsigned checks=0;
void Check(bool value,const char* message) {
    ++checks; if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F f,const char* message) {
    bool rejected=false;
    try { f(); } catch (const std::exception&) { rejected=true; }
    Check(rejected,message);
}
void GroupMasks() {
    using mt4copier::DealerGroupAllowed;
    for (const auto& [mask,group,expected]:std::vector<std::tuple<std::string,std::string,bool>>{
        {"*","real",true},{"real","real",true},{"Real","real",false},
        {"real","real2",false},{"real","xreal",false},{"real*","real",true},
        {"real*","real-ecn",true},{"*ecn","real-ecn",true},{"*ecn","real-ecn2",false},
        {"real*,demo*","demo-1",true},{"demo*,real*","cent-1",false},
        {"!demo*,*","demo-1",false},{"!demo*,*","real-1",true},
        {"*,!demo*","demo-1",true}, // MT4's first matching term wins.
        {"real*,!real-test","real-test",true},{"!real-test,real*","real-test",false},
        {"!demo*","real-1",false},
        {" real*","real",false},{"real* ","real",false},{"real* ,demo*","demo-1",true},
        {"a?c","abc",false},{"a?c","a?c",true},{"a*c","ac",true},
        {"a*b*c","abc",true},{"a*b*c","ac",false},{"ab*bc","abc",false},
        {"*ab*bc","abc",false}, // Reject overlapping-literal acceptance in the legacy helper.
        {"!*ab*bc,*","abc",false}, // A stricter exclusion must never expand access.
        {"*ab*bc,*","abc",false},{"!a*b,*","a",false},{"a*b,*","a",false},
        {"a*,!*ab*bc","abc",true}, // An earlier unambiguous match retains first-match semantics.
        {"!*ab*bc,*","abbc",false},{"!*ab*bc,*","zzz",true},
        {"ab*bc","abbc",true},{"**a***b**","ab",true},{"**a***b**","xaaby",true},
        {"real\\*","real\\ecn",true},{"real\\*","real/ecn",false},
        {"abc","abc ",false},{"abc ","abc ",true},{"a*,!b*","b",false}}) {
        Check(DealerGroupAllowed(mask,group)==expected,"MT4 group masks preserve exact first-match authorization");
    }
    Check(DealerGroupAllowed(std::string(1023,'*'),std::string(15,'x')),
          "native maximum bounded mask and group sizes accepted");
    for (const auto& mask:std::vector<std::string>{"",",",",*","*,","real,,*","!","!!*","r!eal","*,!",
        "*,!!x",std::string(1024,'*'),"*\n","*\t","*\r",std::string("*\0ignored",9),std::string("*\x7f",2)})
        Reject([&]{DealerGroupAllowed(mask,"real");},"malformed full mask rejected even after an allow term");
    for (const auto& group:std::vector<std::string>{"",std::string(16,'x'),"*","real,demo","!real","real\n",
        std::string("real\0other",10),std::string("real\x7f",5)})
        Reject([&]{DealerGroupAllowed("*",group);},"invalid target group rejected");
    Check(DealerGroupAllowed("*,\xC8\xCB", "\xC8\xCB"),"native high bytes are matched without codepage guessing");
    const std::string distinct="abcdefghijk";
    Check(!DealerGroupAllowed("!*a*b*c*d*e*f*g*h*i*j*k*,*",distinct),
          "legacy recursion-limit disagreement cannot bypass an exclusion");
    Check(!DealerGroupAllowed("*a*b*c*d*e*f*g*h*i*j*k*,*",distinct),
          "legacy recursion-limit disagreement cannot fall through to a broad allow");
    Check(DealerGroupAllowed("*a*b*c*d*e*f*g*h*i*j","abcdefghij"),
          "ten legacy wildcard calls remain supported when both matchers agree");
}
void ManagerAccess() {
    using mt4copier::RequireDealerAccount;
    RequireDealerAccount(1234,1234,true,false,true,"!demo*,real*","real-ecn");
    ++checks;
    RequireDealerAccount(2147483647u,2147483647u,true,false,true,"*","real");
    ++checks;
    Reject([]{RequireDealerAccount(0,0,true,false,true,"*","real");},"placeholder login cannot authorize trades");
    Reject([]{RequireDealerAccount(1234,1235,true,false,true,"*","real");},"manager identity must match requested login");
    Reject([]{RequireDealerAccount(2147483648u,2147483648u,true,false,true,"*","real");},"manager must fit native signed login");
    Reject([]{RequireDealerAccount(1234,1234,false,false,true,"*","real");},"disabled manager cannot authorize trades");
    Reject([]{RequireDealerAccount(1234,1234,true,true,true,"*","real");},"readonly manager cannot authorize trades");
    Reject([]{RequireDealerAccount(1234,1234,true,false,false,"*","real");},"broker permission is required");
    Reject([]{RequireDealerAccount(1234,1234,true,false,true,"demo*","real");},"unmanaged follower group denied");
    Reject([]{RequireDealerAccount(1234,1234,true,false,true,"!real,*","real");},"manager exclusion preserved");
}
void SecurityLimits() {
    using mt4copier::RequireDealerVolume;
    for (int volume:{1,50,100}) { RequireDealerVolume(true,1,100,volume); ++checks; }
    RequireDealerVolume(true,100,100,100); ++checks;
    RequireDealerVolume(true,0,100,1); ++checks;
    RequireDealerVolume(true,1,std::numeric_limits<int>::max(),std::numeric_limits<int>::max()); ++checks;
    Reject([]{RequireDealerVolume(false,1,100,1);},"disabled dealer security group denied");
    for (const auto& limits:std::vector<std::tuple<int,int,int>>{
        {-1,100,1},{0,0,1},{1,0,1},{100,1,100},{1,100,0},{1,100,-1},{2,100,1},{1,100,101}})
        Reject([&]{RequireDealerVolume(true,std::get<0>(limits),std::get<1>(limits),std::get<2>(limits));},
               "invalid or out-of-range native volume denied");
}
}
int main() {
    try {
        GroupMasks(); ManagerAccess(); SecurityLimits();
        std::cout<<checks<<" MT4 dealer policy checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}
