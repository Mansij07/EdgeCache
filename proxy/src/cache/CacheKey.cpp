#include "cache/CacheKey.h"

#include <algorithm>
#include <sstream>
#include <vector>

using namespace std;

namespace edgecache {

std::string normalizeQuery(const std::string& rawQuery) {
    if (rawQuery.empty()) return "";
    std::vector<std::string> params;
    std::string cur;
    std::istringstream is(rawQuery);
    while (std::getline(is, cur, '&')) {
        if (!cur.empty()) params.push_back(cur);
    }
    std::sort(params.begin(), params.end());
    std::string out;
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) out += '&';
        out += params[i];
    }
    return out;
}

CacheKey CacheKey::fromRequest(const HttpRequest& req, const std::string& host) {

    std::string q = normalizeQuery(req.query);
    std::string v;
    v.reserve(req.method.size() + host.size() + req.path.size() + q.size() + 8);
    v += req.method;
    v += '|';
    v += host;
    v += '|';
    v += req.path;
    if (!q.empty()) {
        v += '?';
        v += q;
    }
    return CacheKey{v};
}

}
