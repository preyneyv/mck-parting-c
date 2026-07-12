#include <prism/cartridge_identity.h>

#include <string.h>

typedef struct
{
  uint32_t state[8];
  uint64_t bit_count;
  uint8_t block[64];
  size_t block_size;
} sha256_t;

static uint32_t rotate_right(uint32_t value, uint8_t amount)
{
  return (value >> amount) | (value << (32u - amount));
}

static void sha256_transform(sha256_t *sha, const uint8_t block[64])
{
  static const uint32_t constants[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
  };
  uint32_t words[64];
  for (uint8_t i = 0; i < 16; ++i)
    words[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
  for (uint8_t i = 16; i < 64; ++i)
  {
    uint32_t s0 = rotate_right(words[i - 15], 7) ^
                  rotate_right(words[i - 15], 18) ^ (words[i - 15] >> 3);
    uint32_t s1 = rotate_right(words[i - 2], 17) ^
                  rotate_right(words[i - 2], 19) ^ (words[i - 2] >> 10);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }
  uint32_t a = sha->state[0], b = sha->state[1], c = sha->state[2];
  uint32_t d = sha->state[3], e = sha->state[4], f = sha->state[5];
  uint32_t g = sha->state[6], h = sha->state[7];
  for (uint8_t i = 0; i < 64; ++i)
  {
    uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                    rotate_right(e, 25);
    uint32_t choice = (e & f) ^ (~e & g);
    uint32_t temporary1 = h + sum1 + choice + constants[i] + words[i];
    uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                    rotate_right(a, 22);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temporary2 = sum0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary1;
    d = c;
    c = b;
    b = a;
    a = temporary1 + temporary2;
  }
  sha->state[0] += a;
  sha->state[1] += b;
  sha->state[2] += c;
  sha->state[3] += d;
  sha->state[4] += e;
  sha->state[5] += f;
  sha->state[6] += g;
  sha->state[7] += h;
}

static void sha256_init(sha256_t *sha)
{
  *sha = (sha256_t){
      .state = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
  };
}

static void sha256_update(sha256_t *sha, const void *data, size_t size)
{
  const uint8_t *bytes = data;
  sha->bit_count += (uint64_t)size * 8u;
  while (size > 0)
  {
    size_t available = sizeof(sha->block) - sha->block_size;
    size_t copied = size < available ? size : available;
    memcpy(sha->block + sha->block_size, bytes, copied);
    sha->block_size += copied;
    bytes += copied;
    size -= copied;
    if (sha->block_size == sizeof(sha->block))
    {
      sha256_transform(sha, sha->block);
      sha->block_size = 0;
    }
  }
}

static void sha256_finish(sha256_t *sha, uint8_t digest[32])
{
  sha->block[sha->block_size++] = 0x80;
  if (sha->block_size > 56)
  {
    memset(sha->block + sha->block_size, 0,
           sizeof(sha->block) - sha->block_size);
    sha256_transform(sha, sha->block);
    sha->block_size = 0;
  }
  memset(sha->block + sha->block_size, 0, 56 - sha->block_size);
  for (uint8_t i = 0; i < 8; ++i)
    sha->block[63 - i] = (uint8_t)(sha->bit_count >> (i * 8));
  sha256_transform(sha, sha->block);
  for (uint8_t i = 0; i < 8; ++i)
  {
    digest[i * 4] = (uint8_t)(sha->state[i] >> 24);
    digest[i * 4 + 1] = (uint8_t)(sha->state[i] >> 16);
    digest[i * 4 + 2] = (uint8_t)(sha->state[i] >> 8);
    digest[i * 4 + 3] = (uint8_t)sha->state[i];
  }
}

bool prism_cartridge_id_valid_n(const char *id, size_t length)
{
  if (id == NULL || length == 0 || length > PRISM_CARTRIDGE_ID_MAX)
    return false;
  size_t label_length = 0;
  for (size_t i = 0; i < length; ++i)
  {
    unsigned char character = (unsigned char)id[i];
    if (character == '.')
    {
      if (label_length == 0 || id[i - 1] == '-')
        return false;
      label_length = 0;
      continue;
    }
    bool letter = character >= 'a' && character <= 'z';
    bool digit = character >= '0' && character <= '9';
    if (!letter && !digit && character != '-')
      return false;
    if (label_length == 0 && character == '-')
      return false;
    if (++label_length > 63)
      return false;
  }
  return label_length != 0 && id[length - 1] != '-';
}

bool prism_cartridge_id_valid(const char *id)
{
  if (id == NULL)
    return false;
  size_t length = 0;
  while (length <= PRISM_CARTRIDGE_ID_MAX && id[length] != '\0')
    ++length;
  return prism_cartridge_id_valid_n(id, length);
}

bool prism_app_key_derive_n(const char *id, size_t length,
                            prism_app_key_t app_key)
{
  static const uint8_t domain[] = "prism.app.v1";
  if (app_key == NULL || !prism_cartridge_id_valid_n(id, length))
    return false;
  sha256_t sha;
  uint8_t digest[32];
  sha256_init(&sha);
  sha256_update(&sha, domain, sizeof(domain));
  sha256_update(&sha, id, length);
  sha256_finish(&sha, digest);
  memcpy(app_key, digest, PRISM_APP_KEY_BYTES);
  return true;
}

bool prism_app_key_derive(const char *id, prism_app_key_t app_key)
{
  if (id == NULL)
    return false;
  size_t length = 0;
  while (length <= PRISM_CARTRIDGE_ID_MAX && id[length] != '\0')
    ++length;
  return length <= PRISM_CARTRIDGE_ID_MAX &&
         prism_app_key_derive_n(id, length, app_key);
}

prism_cartridge_update_result_t prism_cartridge_update_check(
    const uint8_t installed_key[PRISM_APP_KEY_BYTES],
    const char *installed_id, uint32_t installed_version,
    const uint8_t candidate_key[PRISM_APP_KEY_BYTES],
    const char *candidate_id, uint32_t candidate_version)
{
  if (installed_key == NULL || candidate_key == NULL ||
      memcmp(installed_key, candidate_key, PRISM_APP_KEY_BYTES) != 0)
    return PRISM_CARTRIDGE_UPDATE_SEPARATE;
  if (installed_id == NULL || candidate_id == NULL ||
      strcmp(installed_id, candidate_id) != 0)
    return PRISM_CARTRIDGE_UPDATE_KEY_COLLISION;
  return candidate_version < installed_version
             ? PRISM_CARTRIDGE_UPDATE_DOWNGRADE
             : PRISM_CARTRIDGE_UPDATE_MATCH;
}
