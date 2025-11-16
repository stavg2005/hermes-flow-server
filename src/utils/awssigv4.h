#ifndef AWS_SIGV4_H
#define AWS_SIGV4_H
// Sign aws request with v4 signature
// http://docs.aws.amazon.com/general/latest/gr/sigv4_signing.html

#include <iostream>
#include <string.h>
#include <sstream> 
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <ctime>
#include <map>
#include <vector>
#include <algorithm>
#include "openssl/sha.h"
#include "openssl/hmac.h"
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>

namespace hcm {
    /* compute and return the MD5 hex string of the given string input */
    inline std::string get_string_md5(const std::string &input_str) {
        std::stringstream ss("");
        unsigned char md5_result[MD5_DIGEST_LENGTH];
        int i;

        MD5((unsigned char*) input_str.c_str(), input_str.length(), md5_result);
        for (i=0; i <MD5_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)md5_result[i];
        }
        return ss.str();
    }
    enum E_SIG_TYPE { SINGLE_CHUNK, SEED_CHUNK, MULTI_CHUNK};
    class Signature
    {
        private:
            std::string m_secret_key, m_access_key, m_service, m_host, m_region, m_signed_headers;
            std::string m_signing_key;

            char m_amzdate[20];
            char m_datestamp[20];

            void setSignatureKey();

            void hashSha256(const std::string &str, unsigned char outputBuffer[SHA256_DIGEST_LENGTH]);

            // digest to hexdigest
            const std::string hexlify(const unsigned char* digest);

            // equals to hashlib.sha256(str).hexdigest()
            const std::string sha256Base16(const std::string &str);

            // equals to  hmac.new(key, msg, hashlib.sha256).digest()
            const std::string sign(const std::string &key, const std::string &msg);

            std::map<std::string, std::vector<std::string> > mergeHeaders(
                    std::map<std::string, std::vector<std::string> > &canonical_header_map
                    );
            std::string canonicalHeaderStr(std::map<std::string, std::vector<std::string> > &canonical_header_map);
            std::string signedHeaderStr(std::map<std::string, std::vector<std::string> > &canonical_header_map);

            std::string createCanonicalQueryString(const std::string &query_string);

        public:
            Signature(
                    const std::string service,
                    const std::string host,
                    const std::string region,
                    const std::string secret_key,
                    const std::string access_key,
                    const time_t sig_time=time(0)
                    );

            // Step 1: creaate a canonical request
            std::string createCanonicalRequest(
                    const std::string &method,
                    const std::string &canonical_uri,
                    const std::string &querystring,
                    std::map<std::string, std::vector<std::string> > &canonical_header_map,
                    const std::string &payload,
                    const E_SIG_TYPE st
                    );

            // Step 2: CREATE THE STRING TO SIGN
            std::string createStringToSign(std::string &canonical_request);

            // step 3: CALCULATE THE SIGNATURE
            std::string createSignature(std::string &string_to_sign);

            // Step 4.1: CREATE Authorization header
            // This method assuemd to be called after previous step
            // So It can get credential scope and signed headers
            std::string createAuthorizationHeader(std::string &signature);

            //returns date in ISO8601 format
            std::string getdate() { return m_amzdate; }

            //return sha256 in base16
            const std::string sha256_Base16(const std::string &str){ return sha256Base16(str);};

            //return sha256 in base16
            const std::string getSignatureKey(){ return m_signing_key; }

            //return chunk String to sign
            std::string createChunkStringtoSign(const std::string &previousSig, int chunkSize, const std::string &payload_chunk);

            //return chunk data with size prev signature and chunk payload
            const std::string createChunkData(const std::string &chunkSignature, int chunkSize, const std::string &payload_chunk);

            //return Authorisation header value
            std::string getAuthorization(const std::string &method, const std::string &canonical_uri, const std::string &query_string, const std::string &payload, std::string &payload_hash, const E_SIG_TYPE st = SINGLE_CHUNK);

            //return content length for multipart put
            int calculateContentLength(int total_size, int  chunk_size);
    };
}
#endif
