#pragma once

#include <memory>

namespace runos {
namespace maple {

class Flow {
public:
    virtual const Flow& operator=(const Flow&) {return *this; }
    virtual ~Flow(){ }
};

typedef std::shared_ptr<Flow> FlowPtr;

}
}
