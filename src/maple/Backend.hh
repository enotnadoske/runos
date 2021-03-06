#pragma once

#include "Flow.hh"
#include "oxm/field_set_fwd.hh"
#include "oxm/field.hh"

namespace runos {
namespace maple {

struct Backend {
    virtual void install(unsigned priority,
                         oxm::expirementer::full_field_set const& match,
                         FlowPtr flow) = 0;

    virtual void remove(FlowPtr flow) = 0;
    virtual void remove(unsigned priority,
                        oxm::field_set const& match) = 0;
    virtual void remove(oxm::field_set const& match) = 0;

    // test would be repeated in match
    virtual void barrier_rule(unsigned priority,
                            oxm::expirementer::full_field_set const& match,
                            oxm::field<> const& test,
                            uint64_t id) = 0;
    virtual void barrier() { }
};

} // namespace maple
} // namespace runos
