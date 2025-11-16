#include "awssigv4.h"
using namespace std;

namespace hcm {

    // Helper function for trim string
    // trim from start
    static inline std::string &ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(),
                                    [](unsigned char ch){ return !std::isspace(ch); }));
    return s;
}

// trim from end
static inline std::string &rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(),
                         [](unsigned char ch){ return !std::isspace(ch); }).base(),
            s.end());
    return s;
}

// trim from both ends
static inline std::string &trim(std::string &s) {
    return ltrim(rtrim(s));
}

    Signature::Signature(
            const std::string service,
            const std::string host,
            const std::string region,
            const std::string secret_key,
            const std::string access_key,
            const time_t sig_time
            )
    {
        m_service = service;
        m_host = host;
        m_region = region;
        m_secret_key = secret_key;
        m_access_key = access_key;

        //
        // Create a date for headers and the credential string
        struct tm  *tstruct = gmtime(&sig_time);
        strftime(m_amzdate, sizeof(m_amzdate), "%Y%m%dT%H%M%SZ", tstruct);
        strftime(m_datestamp, sizeof(m_datestamp), "%Y%m%d", tstruct);
        // setting m_signing_key
        setSignatureKey();
    }

    const std::string Signature::createChunkData(const std::string &chunkSignature, int chunkSize, const std::string &payload_chunk)
    {
        std::stringstream sstream;
        sstream << std::hex << chunkSize;
        std::string result = sstream.str() + ";chunk-signature=" + chunkSignature + "\r\n" + payload_chunk + "\r\n";
        //cout << "------> " << result << endl;

        return result;
    }

    std::string Signature::createChunkStringtoSign(const std::string &previousSig, int chunkSize, const std::string &payload_chunk)
    {
        std::string algorithm = "AWS4-HMAC-SHA256-PAYLOAD";
        std::string credential_scope = std::string(m_datestamp) + "/" + m_region + "/" + m_service + "/" + "aws4_request";
        std::string string_to_sign = algorithm + '\n' + m_amzdate + '\n' +  credential_scope + '\n' + previousSig + '\n' + "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" + '\n' + sha256Base16(payload_chunk);
        return string_to_sign;
    }


    void Signature::hashSha256(const std::string &str, unsigned char outputBuffer[SHA256_DIGEST_LENGTH])
    {
        auto length = str.length();
        //std::cout <<"string length" <<  str.length() << std::endl;
        char *c_string = new char [length + 1];
        std::memcpy(c_string, (unsigned char *)str.c_str(), length);
        c_string[length] = '\0';
        //std::cout <<"string length" << length  << std::endl;

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, c_string, length);
        SHA256_Final(hash, &sha256);

        for (int i=0;i<SHA256_DIGEST_LENGTH;i++) {
            outputBuffer[i] = hash[i];
        }

        delete [] c_string;
    }

    const std::string Signature::hexlify(const unsigned char* digest) {

        char outputBuffer[65];

        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            sprintf(outputBuffer + (i * 2), "%02x", digest[i]);
        }
        outputBuffer[64] = 0;

        return std::string(outputBuffer);

    }

    // equals to hashlib.sha256(str).hexdigest()
    const std::string Signature::sha256Base16(const std::string &str) {
        unsigned char hashOut[SHA256_DIGEST_LENGTH];
        this->hashSha256(str,hashOut);

        return this->hexlify(hashOut);
    }

    // equals to  hmac.new(key, msg.encode('utf-8'), hashlib.sha256).digest()
    const std::string Signature::sign(const std::string &key, const std::string &msg)
    {
        unsigned char *c_key = new unsigned char[key.length() + 1];
        memcpy(c_key, (unsigned char *)key.data(), key.length());

        unsigned char *c_msg = new unsigned char[msg.length() + 1];
        memcpy(c_msg, (unsigned char *)msg.data(), msg.length());

        unsigned char * digest = HMAC(EVP_sha256(), (unsigned char*)c_key, key.length(), c_msg, msg.length(), NULL, NULL);

        delete[] c_key;
        delete[] c_msg;

        std::string signed_str = std::string((char *)digest, 32);

        return signed_str;
    }

    void Signature::setSignatureKey()
    {
        std::string kDate = sign("AWS4" + m_secret_key, m_datestamp);
        std::string kRegion = sign(kDate, m_region);
        std::string kService = sign(kRegion, m_service);
        std::string kSigning = sign(kService, "aws4_request");
        m_signing_key = kSigning;
    }

    std::map<std::string, std::vector<std::string> > Signature::mergeHeaders(
            std::map<std::string, std::vector<std::string> > &canonical_header_map)
    {
        std::map<std::string, std::vector<std::string> > merge_header_map;
        std::map<std::string, std::vector<std::string> >::iterator search_it;

        for (std::map<std::string, std::vector<std::string> >::iterator it=canonical_header_map.begin(); it != canonical_header_map.end(); it++)
        {
            std::string header_key = it->first;
            std::transform(header_key.begin(), header_key.end(), header_key.begin(), ::tolower);
            header_key = trim(header_key);

            search_it = merge_header_map.find(header_key);

            if (search_it == merge_header_map.end())
            {
                merge_header_map[header_key];
            }
            for (std::vector<std::string>::iterator lit=it->second.begin(); lit != it->second.end(); lit++)
            {
                std::string header_value = *lit;
                header_value = trim(header_value);
                merge_header_map[header_key].push_back(header_value);
            }
        }

        for (std::map<std::string, std::vector<std::string> >::iterator it=merge_header_map.begin(); it != merge_header_map.end(); it++)
        {
            std::sort(it->second.begin(), it->second.end());
        }

        return merge_header_map;
    }

    std::string Signature::canonicalHeaderStr(
            std::map<std::string, std::vector<std::string> > &canonical_header_map)
    {
        std::string canonical_headers = "";
        for (std::map<std::string, std::vector<std::string> >::iterator it=canonical_header_map.begin(); it != canonical_header_map.end(); it++)
        {
            canonical_headers += it->first + ":";
            for(std::vector<std::string>::iterator yit=it->second.begin(); yit != it->second.end();)
            {
                canonical_headers += *yit;

                if(++yit != it->second.end())
                    canonical_headers += ",";
            }
            canonical_headers += "\n";
        }

        return canonical_headers;
    }

    std::string Signature::signedHeaderStr(
            std::map<std::string, std::vector<std::string> > &canonical_header_map)
    {
        std::string signed_header = "";
        for (std::map<std::string, std::vector<std::string> >::iterator it=canonical_header_map.begin(); it != canonical_header_map.end();)
        {
            signed_header += it->first;

            if(++it != canonical_header_map.end())
                signed_header += ";";
        }
        return signed_header;
    }

    std::string Signature::createCanonicalQueryString(const std::string &query_string)
    {
        std::map<std::string, std::vector<std::string> > query_map;

        std::stringstream qss(query_string);
        std::string query_pair;

        while(std::getline(qss, query_pair, '&'))
        {
            std::size_t epos = query_pair.find("=");
            std::string query_key, query_val;

            if (epos != std::string::npos)
            {
                query_key = query_pair.substr(0, epos);
                query_val = query_pair.substr(epos+1);
            }

            std::map<std::string, std::vector<std::string> >::iterator search_it = query_map.find(query_key);

            if (search_it == query_map.end())
            {
                query_map[query_key];
            }

            query_map[query_key].push_back(query_val);
        }

        for (std::map<std::string, std::vector<std::string> >::iterator it=query_map.begin(); it != query_map.end(); it++)
        {
            std::sort(it->second.begin(), it->second.end());
        }

        std::string canonical_query_string = "";
        for (std::map<std::string, std::vector<std::string> >::iterator it=query_map.begin(); it != query_map.end();)
        {
            for(std::vector<std::string>::iterator yit=it->second.begin(); yit != it->second.end();)
            {
                canonical_query_string += it->first + "=" + *yit;

                if(++yit != it->second.end())
                    canonical_query_string += "&";
            }

            if(++it != query_map.end())
                canonical_query_string += "&";
        }

        return canonical_query_string;

    }

    std::string Signature::createCanonicalRequest(
            const std::string &method,
            const std::string &canonical_uri,
            const std::string &querystring,
            std::map<std::string, std::vector<std::string> > &canonical_header_map,
            const std::string &payload,
            const E_SIG_TYPE st
            )
    {

        // Step 1: create canonical request
        // http://docs.aws.amazon.com/general/latest/gr/sigv4-create-canonical-request.html

        // Step 1.1 define the verb (GET, POST, etc.)
        // passed in as argument

        // Step 1.2: Create canonical URI--the part of the URI from domain to query
        // string (use '/' if no path)
        // passed in as argument

        // Step 1.3: Create the canonical query string. In this example (a GET request),
        // request parameters are in the query string. Query string values must
        // be URL-encoded (space=%20). The parameters must be sorted by name.
        // For this example, the query string is pre-formatted in the request_parameters variable.
        // passed in as argument

        // Step 1.4: Create the canonical headers and signed headers. Header names
        // and value must be trimmed and lowercase, and sorted in ASCII order.
        // Note that there is a trailing \n.

        std::map<std::string, std::vector<std::string> > merged_headers = mergeHeaders(canonical_header_map);

        std::string canonical_headers = canonicalHeaderStr(merged_headers);

        // Step 1.5: Create the list of signed headers. This lists the headers
        // in the canonical_headers list, delimited with ";" and in alpha order.
        // Note: The request can include any headers; canonical_headers and
        // signed_headers lists those that you want to be included in the
        //hash of the request. "Host" and "x-amz-date" are always required.
        m_signed_headers = signedHeaderStr(merged_headers);

        // Step 1.6: Create payload hash (hash of the request body content). For GET
        // requests, the payload is an empty string ("").
        std::string payload_hash = "UNSIGNED-PAYLOAD";
        if(SINGLE_CHUNK == st)
        {
            if(method != "HEAD" && method !="DELETE" && method !="GET")
            {
                payload_hash = sha256Base16(payload);
            }
        }
        else if(SEED_CHUNK == st)
        {
            payload_hash = "STREAMING-AWS4-HMAC-SHA256-PAYLOAD";
        }
        else
        {
            payload_hash = sha256Base16(payload);
        }
        //std::cout << "payload hash " << payload_hash << std::endl;

        // Step 1.7: Combine elements to create create canonical request

        // generate canonical query string
        std::string canonical_querystring = createCanonicalQueryString(querystring);
        //std::cout << " canonical_querystring : " << canonical_querystring << std::endl;

        std::string canonical_request = method + "\n" + canonical_uri + "\n" + canonical_querystring + "\n" + canonical_headers + "\n" + m_signed_headers + "\n" + payload_hash;

        return canonical_request;
    }


    std::string Signature::createStringToSign(std::string &canonical_request)
    {
        // Step 2: CREATE THE STRING TO SIGN
        // http://docs.aws.amazon.com/general/latest/gr/sigv4-create-string-to-sign.html
        // Match the algorithm to the hashing algorithm you use, either SHA-1 or
        // SHA-256 (recommended)

        std::string algorithm = "AWS4-HMAC-SHA256";
        std::string credential_scope = std::string(m_datestamp) + "/" + m_region + "/" + m_service + "/" + "aws4_request";
        std::string string_to_sign = algorithm + '\n' +  m_amzdate + '\n' +  credential_scope + '\n' +  sha256Base16(canonical_request);

        return string_to_sign;
    }

    std::string Signature::createSignature(std::string &string_to_sign)
    {
        // step 3: CALCULATE THE SIGNATURE
        // http://docs.aws.amazon.com/general/latest/gr/sigv4-calculate-signature.html
        // Create the signing key using the function defined above.
        std::string signing_key = this->getSignatureKey();

        // Sign the string_to_sign using the signing_key
        std::string signature_str = sign(signing_key, string_to_sign);

        unsigned char *signature_data = new unsigned char[signature_str.length() + 1];
        memcpy(signature_data, (unsigned char *)signature_str.data(), signature_str.length());

        std::string signature = hexlify(signature_data);

        delete[] signature_data;

        return signature;
    }


    std::string Signature::createAuthorizationHeader(std::string &signature)
    {

        std::string algorithm = "AWS4-HMAC-SHA256";
        std::string credential_scope = std::string(m_datestamp) + "/" + m_region + "/" + m_service + "/" + "aws4_request";

        return algorithm + " " + "Credential=" + m_access_key + "/" + credential_scope + ", " +  "SignedHeaders=" + m_signed_headers + ", " + "Signature=" + signature;
    }

    std::string	Signature::getAuthorization(const std::string &method, const std::string &canonical_uri, const std::string &query_string, const std::string &payload, std::string &payload_hash, const E_SIG_TYPE st)
    {
        std::map<std::string, std::vector<std::string> > header_map;

        if(st == SINGLE_CHUNK)
        {
            if(method != "HEAD" && method !="DELETE" && method !="GET")
            {
                payload_hash = sha256Base16(payload);
            }
            else
            {
                payload_hash = "UNSIGNED-PAYLOAD";
            }
        }
        else if(SEED_CHUNK == st)
        {
            //payload_hash = "UNSIGNED-PAYLOAD";
            payload_hash = "STREAMING-AWS4-HMAC-SHA256-PAYLOAD";
            header_map["x-amz-decoded-content-length"].push_back(std::to_string(payload.length()));
            header_map["content-encoding"].push_back("aws-chunked");
            //header_map["content-length"].push_back("aws-chunked");
            //header_map["x-amz-storage-class"].push_back("REDUCED_REDUNDANCY");
        }
        else
        {
            payload_hash = sha256Base16(payload);
        }
        //cout << "hash " << payload_hash << endl;

        header_map["Host"].push_back(m_host);
        //header_map["Content-Type"].push_back("application/x-www-form-urlencoded");
        header_map["Content-Type"].push_back("application/octet-stream");
        //header_map["Content-Type"].push_back("application/json");
        header_map["x-amz-content-sha256"].push_back(payload_hash);
        header_map["x-amz-date"].push_back(m_amzdate);

        auto canonical_req = createCanonicalRequest(method, canonical_uri, query_string, header_map, payload, st);

        //cout << "canonical_request-------\n" << canonical_req << "\n-------------" << std::endl;
        // Step 2: CREATE THE STRING TO SIGN
        auto string_to_sign = createStringToSign(canonical_req);

        //cout << "string to sign :\n" << string_to_sign << "\n-------------" << std::endl;
        // step 3: CALCULATE THE SIGNATURE
        auto sig = createSignature(string_to_sign);

        // Step 4.1: CREATE Authorization header
        // This method assuemd to be called after previous step
        // So It can get credential scope and signed headers
        auto auth =  createAuthorizationHeader(sig);
        //return createAuthorizationHeader(sig);

        //cout << "getAuthorisation : Authorization : " << auth << std::endl;
        return auth;
    }

    int Signature::calculateContentLength(int total_size, int  chunk_size)
    {
        int totallength = total_size;
        std::stringstream sstream_chunk, sstream_lastchunk;

        int nChunks = (total_size/chunk_size) + 2;
        int lastChunkSize = total_size % chunk_size;

        sstream_chunk << std::hex << chunk_size;
        /*
         * chunk format (as below):
         * string(IntHexBase(chunk-size)) + ";chunk-signature=" + signature + \r\n + chunk-data + \r\n
         * ==============================
         * there will be two \r\n in every chunks 2 * 2 * nChunks
         * there will be 64 bit signature prefixed by ";chunk-signature=" 17 bytes in every chunks
         * ( 17  * 64 ) * nChunks
         * last chunk data size is 0  --> 1
         * previous chunk is less thank actual chunks --> sstream_lastchunk.str().length()...this chunk may not exist as well
         * remaining chunks size will be same -> sstream_chunk.str().length() * ( nChunks - 2 )
         */
        if(0 == lastChunkSize)
        {
            nChunks--;// as second last wont be a partial chunk
            totallength += 2 * 2 * nChunks + ( 17 + 64 ) * nChunks  + sstream_chunk.str().length() * ( nChunks - 1 ) + 1;
        }
        else
        {
            sstream_lastchunk << std::hex << lastChunkSize;
            totallength += 2 * 2 * nChunks + ( 17 + 64 ) * nChunks  + sstream_chunk.str().length() * ( nChunks - 2 ) + sstream_lastchunk.str().length() + 1;
        }
        //cout << " Signature::calculateContentLength : totallength : " << totallength << endl;
        return totallength ;
    }
}
