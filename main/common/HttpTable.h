/*
 * HttpTable.h
 */

#pragma once

#include "HttpHelper.h"

template<size_t N> class Row
{
    std::string cell[N] {};
public:
    Row() {};
    std::string & operator[](size_t i) { return cell[i<N?i:N-1]; };

    void AddTo( HttpHelper & hh ) {
        for (size_t i = 0; i < N; ++i) {
            hh.Add( "<td>"  );
            hh.Add( cell[i] );
            hh.Add( "</td>" );
        }
    };
};

template<size_t R, size_t C> class Table
{
    Row<C> row[R] {};
public:
    Table() {};
    Row<C>& operator[](size_t i) { return row[i<R?i:R-1]; };

    void AddTo( HttpHelper & hh ) {
        for (size_t i = 0; i < R; ++i) {
            hh.Add( "    <tr>" );
            row[i].AddTo( hh );
            hh.Add( "</tr>\n" );
        }
    };
};
