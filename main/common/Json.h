/*
 * Json.h
 *
 * we don't have runtime type info -> need manually casting
 */
#if 0
# define JSON_DEBUG
#else
# define JSON_NDEBUG
#endif

#include <string>
#include <vector>
#include <map>

class JsonStr;
class JsonNum;
class JsonVec;
class JsonMap;

class JsonObj
{
public:
    typedef std::string                 str_t;
    typedef float                       num_t;
    typedef std::vector<JsonObj *>      vec_t;
    typedef std::map<str_t, JsonObj *>  map_t;

    JsonObj() = default;      // temporary needed for creating tuple in map
    JsonObj( char type ) : mType{ type } {}

    // virtual ~JsonObj() {};

    virtual const char * Parse( const char * input );

    bool operator!() const { return mType == 0; }
    char Type() const { return mType; }

#ifdef JSON_DEBUG
    virtual void Dump( int indent = 0 ) const;
#endif

    const JsonStr * asStr() const { return mType == 's' ? (JsonStr *) this : nullptr; };
    const JsonNum * asNum() const { return mType == 'n' ? (JsonNum *) this : nullptr; };
    const JsonVec * asVec() const { return mType == 'v' ? (JsonVec *) this : nullptr; };
    const JsonMap * asMap() const { return mType == 'm' ? (JsonMap *) this : nullptr; };

    virtual const str_t * Str() const { return mType == 's' ? mData.str : nullptr; }
    virtual const num_t * Num() const { return mType == 'n' ? mData.num : nullptr; }
    virtual const vec_t * Vec() const { return mType == 'v' ? mData.vec : nullptr; }
    virtual const map_t * Map() const { return mType == 'm' ? mData.map : nullptr; }

    // [i]: when this is JsonVec: return pointer to obj by index
    // [s]: when this is JsonMap: return pointer to obj by key
    const JsonObj & operator[]( int index ) const;
    const JsonObj & operator[]( const str_t & key ) const;
    const JsonObj & operator[]( const char  * key ) const;

protected:
    char mType{ '?' };
    // need a buffer so data is copied too on map and vector assignment
    union {
        str_t * str;
        num_t * num;
        vec_t * vec;
        map_t * map;
    } mData;
};

class JsonStr : public JsonObj
{
public:
    JsonStr() : JsonObj('s') { mData.str = new str_t{}; }
    ~JsonStr() { delete mData.str; }

    const char  * Parse( const char * input ) override;
    const str_t * Str() const override { return mData.str; }
};

class JsonNum : public JsonObj
{
public:
    JsonNum() : JsonObj('n') { mData.num = new num_t{}; }
    ~JsonNum() { delete mData.vec; }

    const char  * Parse( const char * input ) override;
    const num_t * Num() const override { return mData.num; }
};

class JsonVec : public JsonObj
{
public:
    JsonVec() : JsonObj('v') { mData.vec = new vec_t{}; }
    ~JsonVec() { delete mData.vec; };

    const char    * Parse( const char * input ) override;
    const vec_t   * Vec() const override { return mData.vec; }
};

class JsonMap : public JsonObj
{
public:
    JsonMap( const char * data ); // outest parser
    JsonMap() : JsonObj('m') { mData.map = new map_t{}; }
    ~JsonMap() { delete mData.map; }

    const char    * Parse( const char * input ) override;
    const map_t   * Map() const override { return mData.map; }
};
