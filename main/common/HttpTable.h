/*
 * HttpTable.h
 */

#pragma once

#include "HttpHelper.h"

template<size_t N> class Row
{
    std::string mCell[N] {};
public:
    Row() {};
    std::string & operator[](size_t i) { return mCell[i<N?i:N-1]; };

    void AddTo( HttpHelper & hh, size_t alignRight, size_t alignCenter, bool headrow, uint8_t headcols ) {
        for (size_t c = 0; c < N; ++c) {
            std::string tag{"td"};
            if (headrow || (c < headcols))
                tag = "th";
            std::string cell{ "<" + tag };
            if ((alignRight >> c) & 1)
                cell += " align=\"right\"";
            else if ((alignCenter >> c) & 1)
                cell += " align=\"center\"";
            cell += ">";
            cell += mCell[c];
            cell += "</" + tag + ">";
            hh.Add( cell );
        }
    };
};

template<size_t R, size_t C> class Table
{
    Row<C> mRow[R] {};
    size_t mAlignRight {0};
    size_t mAlignCenter {0};
public:
    Table() {};
    Row<C>& operator[](size_t i) { return mRow[i<R?i:R-1]; };

    void Right(  size_t col ) { mAlignRight  |= 1 << col; };
    void Center( size_t col ) { mAlignCenter |= 1 << col; };
    void ResetAlign() {
        mAlignRight = 0;
        mAlignCenter = 0;
    };

    void AddTo( HttpHelper & hh, uint8_t headrows = 0, uint8_t headcols = 0 ) {
        for (size_t r = 0; r < R; ++r) {
            hh.Add( "    <tr>" );
            mRow[r].AddTo( hh, mAlignRight, mAlignCenter, r < headrows, headcols );
            hh.Add( "</tr>\n" );
        }
    };
};
