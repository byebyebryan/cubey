#include <cubey/asset/file_digest.h>

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

namespace cubey::asset {

bool is_sha256_hex(std::string_view value) noexcept {
    return value.size() == 64U && std::ranges::all_of(value, [](char digit) {
               return (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f');
           });
}

std::string sha256_hex(std::span<const std::byte> bytes) {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(),
                                                                          EVP_MD_CTX_free);
    if (!context) {
        throw std::runtime_error("failed to allocate SHA-256 context");
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0U;
    if (EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1 ||
        EVP_DigestFinal_ex(context.get(), digest.data(), &digest_size) != 1) {
        throw std::runtime_error("failed to compute SHA-256");
    }
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(static_cast<std::size_t>(digest_size) * 2U);
    for (unsigned int index = 0U; index < digest_size; ++index) {
        const unsigned char value = digest[index];
        result.push_back(kHexDigits[value >> 4U]);
        result.push_back(kHexDigits[value & 0x0FU]);
    }
    return result;
}

} // namespace cubey::asset
