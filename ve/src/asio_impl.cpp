// ----------------------------------------------------------------------------
// asio_impl.cpp - Separate compilation unit for asio implementation
// ----------------------------------------------------------------------------
// Compiles all asio + asio SSL implementation into a single TU so that symbols
// are subject to -fvisibility=hidden and -fno-gnu-unique, preventing ODR
// conflicts with libfastrtps.so / other libraries that also embed asio.
//
// OpenSSL 3.x-only APIs in ssl/impl/context.ipp are guarded by
// OPENSSL_VERSION_NUMBER >= 0x30000000L, so this compiles cleanly on
// Ubuntu 20.04 / OpenSSL 1.1.x as well.
//
// This file is only compiled when ASIO_SEPARATE_COMPILATION is defined.
// On platforms using ASIO_HEADER_ONLY (Windows/macOS/iOS/Android), this is a no-op.
// ----------------------------------------------------------------------------

#ifdef ASIO_SEPARATE_COMPILATION
#include <asio/impl/src.hpp>
#include <asio/ssl/impl/src.hpp>
#endif
