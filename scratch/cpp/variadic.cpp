//
// Created by Ugo Varetto on 7/29/16.
//
#include <iostream>
#include <cstdlib>
#include <tuple>
#include <future>
#include <algorithm>

using namespace std;

template < typename T, typename...ArgsT >
void Foo(const T& t, const ArgsT&...args, int i = 1) {
    cout << i << endl;
};

int NewID() {
    int i;
    cin >> i;
    return i;
}

std::promise< int > p;

std::future< int > Get() {
    std::future< int > f = p.get_future();
    p.set_value(4);
    return f;
}

void foo(int i = NewID()) {
    cout << i;
}


int main(int argc, char** argv) {
    bool sub = find_if(argv, argv + argc, [](char* s){
        return std::string(s) == "-sub";}) != argv + argc;
    cout << boolalpha << sub << endl;
    auto f = Get();
    cout << f.get() << endl;
    std::tuple<> t;
    Foo<double, float, int>(1.,2.f,3);
    return EXIT_SUCCESS;
}
