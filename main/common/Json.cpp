/*
 * Json.cpp
 */
//define LOG_LOCAL_LEVEL ESP_LOG_DEBUG

#include "Json.h"

#include <esp_log.h>

namespace {
    const char          * TAG = "Json";
    static const JsonObj  NullObj{ 0 };  // the NullObj

const char * parseError( const char * input, const char * cp, const char * objType, const char * hint )
{
    ESP_LOGE( TAG, "parse error: type %s"
                    " / input \"%s\""
                    " / offset %d (\"...%.8s...\")"
                    " / hint \"%s\"",
                    objType, input, cp - input, cp, hint );
    return input;
}

}


const JsonObj & JsonObj::operator[]( int index ) const
{
    if (mType != 'v')
        return NullObj;
    if (index >= mData.vec->size())
        return NullObj;
    return *(*mData.vec)[index];
}

const JsonObj & JsonObj::operator[]( const JsonObj::str_t & key ) const
{
    if (mType != 'm')
        return NullObj;
    const auto & it = mData.map->find( key );
    if (it == mData.map->end() )
        return NullObj;
    return *it->second;
}

const JsonObj & JsonObj::operator[]( const char * key ) const
{
    JsonObj::str_t s{key};
    return operator[](s);
}

const char * JsonObj::Parse( const char * input )
{
    switch (mType)
    {
        case 's': return ((JsonStr *) this)->Parse( input );
        case 'n': return ((JsonNum *) this)->Parse( input );
        case 'v': return ((JsonVec *) this)->Parse( input );
        case 'm': return ((JsonMap *) this)->Parse( input );
    }
    return parseError( input, 0, "?", "cannot parse into None" );
}

const char * JsonStr::Parse( const char * const input )
{
    if (*input != '"')
        return parseError( input, 0, "str", "opening \"" );

    const char * cp = input;
    while (*++cp) {
        if ((*cp == '\\') && cp[1]) {
            (*mData.str) += *++cp;
        } else if (*cp == '"') {
            while (isspace( *++cp ));
            ESP_LOGD( TAG, "parsed str \"%s\"", mData.str->c_str() );
            return cp;
        }
        (*mData.str) += *cp;
    }
    return parseError( input, cp, "str", "closing \"" );
}

const char * JsonNum::Parse( const char * const input )
{
    bool neg = false;
    const char * cp = input;

    if (*cp == '-') neg = true;
    else if (*cp != '+') --cp;

    if ((cp[1] != '.') && ! isdigit(cp[1]))
        return parseError( input, cp + 1, "num", "0..9 or ." );

    ulong val = 0;
    while (isdigit(*++cp)) {
        val *= 10;
        val += *cp - '0';
    }
    float x = val;
    if (*cp == '.') {
        val = 0;
        float frac = 1;
        while (isdigit(*++cp)) {
            val *= 10;
            val += *cp - '0';
            frac *= 10.0;
        }
        x += val / frac;
    }
    if (neg)
        x = - x;
    (*mData.num) = x;
    while (isspace(*cp)) ++cp;
    ESP_LOGD( TAG, "parsed num \"%d\"", (int) (x) );
    return cp;
}

const char * JsonVec::Parse( const char * const input )
{
    if (*input != '[')
        return parseError( input, 0, "array", "opening [" );

    const char *cp = input;
    while (isspace(*++cp));

    while (true) {
        const char * const start = cp;
        if (*cp == '{') {
            JsonMap * map = new JsonMap();
            cp = map->Parse( cp );
            mData.vec->push_back(map);
        } else if (*cp == '[') {
            JsonVec * vec = new JsonVec();
            cp = vec->Parse( cp );
            mData.vec->push_back(vec);
        } else if (*cp == '"') {
            JsonStr * str = new JsonStr();
            cp = str->Parse( cp );
            mData.vec->push_back(str);
        } else {
            JsonNum * num = new JsonNum();
            cp = num->Parse( cp );
            mData.vec->push_back(num);
        }
        if (start == cp)
            return parseError( input, cp, "array", "element parse error" );

        if (*cp == ']')
            break;
        if (*cp != ',')
            return parseError( input, cp, "array", ", or ]" );
        while (isspace(*++cp));
    }

    ESP_LOGD( TAG, "parsed vector of %d elements", mData.vec->size() );
    while (isspace(*++cp));
    return cp;
}

const char * JsonMap::Parse( const char * input )
{
    if (*input != '{')
        return parseError( input, 0, "dict", "opening {" );

    const char *cp = input;
    while (isspace(*++cp));

    while (true) {
        const char * start = cp;
        if (*cp != '"')
            return parseError( input, cp, "dict", "opening \" for key" );

        JsonStr keyObj{};
        cp = keyObj.Parse( cp );
        if (cp == start)
            return parseError( input, cp, "dict", "key parsing" );

        if (*cp != ':')
            return parseError( input, cp, "dict", ":" );
        while (isspace(*++cp));

        str_t key = *keyObj.Str();
        start = cp;
        if (*cp == '{') {
            JsonMap * map = new JsonMap();
            cp = map->Parse( cp );
            (*mData.map)[key] = map;
        } else if (*cp == '[') {
            JsonVec * vec = new JsonVec();
            cp = vec->Parse( cp );
            (*mData.map)[key] = vec;
        } else if (*cp == '"') {
            JsonStr * str = new JsonStr();
            cp = str->Parse( cp );
            (*mData.map)[key] = str;
        } else {
            JsonNum * num = new JsonNum();
            cp = num->Parse( cp );
            (*mData.map)[key] = num;
        }
        if (start == cp)
            return parseError( input, cp, "dict", "value parsing" );

        if (*cp == '}')
            break;
        if (*cp != ',')
            return parseError( input, cp, "dict", ", or }" );
        while (isspace(*++cp));
    }

    ESP_LOGD( TAG, "parsed map of %d elements", mData.map->size() );
    while (isspace(*++cp));
    return cp;
}

JsonMap::JsonMap( const char * data ) : JsonObj('m')
{
    mData.map = new map_t{};

    const char * cp = data;
    while (isspace(*cp)) ++cp;

    const char * end = Parse( cp );

    if (end == cp)
        parseError( data, end, "json", "not json" );
}

#ifdef JSON_DEBUG

void JsonObj::Dump( int indent ) const
{
    switch (mType)
    {
        case 's':
            ESP_LOGD( TAG, "%*s: \"%s\"", indent + 1, "s", mData.str->c_str() );
            break;
        case 'n':
            ESP_LOGD( TAG, "%*s: %d", indent + 1, "n", (int) (*mData.num) );
            break;
        case 'v':
            ESP_LOGD( TAG, "%*s: [", indent + 1, "v" );
            for (const auto & it : *mData.vec)
                it->Dump( indent + 8 );
            ESP_LOGD( TAG, "%*s  ]", indent + 1, "" );
            break;
        case 'm':
            ESP_LOGD( TAG, "%*s: {", indent + 1, "m" );
            for (const auto & it : *mData.map) {
                ESP_LOGD( TAG, "%*s\"%s\":", indent + 4, "", it.first.c_str() );
                it.second->Dump( indent + 8 );
            }
            ESP_LOGD( TAG, "%*s  }", indent + 1, "" );
            break;

        case 0:
            ESP_LOGE( TAG, "%*s: unexpected null type", indent + 1, "0" );
            break;
        default:
            ESP_LOGE( TAG, "%*s%c: unexpected type", indent + 1, "", mType );
            break;
    }
}

#endif
