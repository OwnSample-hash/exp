#pragma once

#define EXPLO_LIB_TLS

#include <config.hpp>
#include <itls.hpp>
#include <type_traits>

#include "tls_includer.hpp"

namespace explo::lib {

static_assert(std::is_base_of_v<ITLSClient, TLSClient> == true && std::is_trivially_copyable_v<TLSClient> == false &&
              std::is_trivially_move_assignable_v<TLSClient> == false &&
              std::is_trivially_move_constructible_v<TLSClient> == false);

inline TLSClient *createTLSClient() { return impl::createTLSClient(); }

} // namespace explo::lib
// Vim: set expandtab tabstop=2 shiftwidth=2 cc=120:
