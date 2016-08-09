//
// Created by Ugo Varetto on 7/29/16.
//
#include <iostream>
#include <cstdlib>
#include <tuple>

using namespace std;

template < typename T, typename...ArgsT >
void Foo(const T& t, const ArgsT&...args, int i = 1) {
    cout << i << endl;
};


int main(int, char**) {
    std::tuple<> t;
    Foo<double, float, int>(1.,2.f,3);
    return EXIT_SUCCESS;
}
