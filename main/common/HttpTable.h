/*
 * HttpTable.h
 */

#pragma once

#include "HttpHelper.h"

template<size_t N> class Row
{
    std::string mCell[N] {};
    size_t mAlignRight  {0};
    size_t mAlignCenter {0};
    size_t mUnite       {0};
public:
    void Right(  size_t col ) { mAlignRight  |= 1 << col; }
    void Center( size_t col ) { mAlignCenter |= 1 << col; }
    void Unite(  size_t col ) { mUnite       |= 1 << col; }
    void ResetAlign() {
        mAlignRight = 0;
        mAlignCenter = 0;
        mUnite = 0;
    }

    Row() {};
    std::string & operator[](size_t i) { return mCell[i<N?i:N-1]; };

    void AddTo( HttpHelper & hh, size_t alignRight, size_t alignCenter, bool headrow, uint8_t headcols ) {
        for (size_t c = 0; c < N; ++c) {
            std::string tag{ "td" };
            if (headrow || (c < headcols))
                tag = "th";
            std::string cell{ "<" + tag };
            if (! (headrow || (c < headcols))) {
                if ((mAlignRight >> c) & 1)
                    cell += " align=\"right\"";
                else if ((mAlignCenter >> c) & 1)
                    cell += " align=\"center\"";
                else if ((alignRight >> c) & 1)
                    cell += " align=\"right\"";
                else if ((alignCenter >> c) & 1)
                    cell += " align=\"center\"";
            }
            if (((mUnite >> c) & 1) && ! mCell[c].empty()) {
                cell += " colspan=2";
            }
            cell += ">";
            cell += mCell[c];
            cell += "</" + tag + ">";
            hh.Add( cell );
            if (((mUnite >> c) & 1) && ! mCell[c].empty())
                ++c;
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
    void Right(  size_t row, size_t col ) { mRow[row].Right(  col ); };
    void Center( size_t row, size_t col ) { mRow[row].Center( col ); };
    void Unite(  size_t row, size_t col ) { mRow[row].Unite(  col ); };

    void ResetAlign() {
        mAlignRight = 0;
        mAlignCenter = 0;
        for (size_t r = 0; r < R; ++r)
            mRow[r].ResetAlign();
    };

    void AddTo( HttpHelper & hh, uint8_t headrows = 0, uint8_t headcols = 0 ) {
        for (size_t r = 0; r < R; ++r) {
            hh.Add( "    <tr>" );
            mRow[r].AddTo( hh, mAlignRight, mAlignCenter, r < headrows, headcols );
            hh.Add( "</tr>\n" );
        }
    };
};
