#pragma once
#include "origin/OriginClient.h"

namespace edgecache {

class HttpOriginClient : public OriginClient {
public:
    OriginResult fetch(const HttpRequest& req, const OriginTarget& target) override;
};

}
