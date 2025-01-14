/*
 * libdigidocpp
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "crypto/TSL.h"

#include "Conf.h"
#include "crypto/Connect.h"
#include "crypto/Digest.h"
#include "util/DateTime.h"
#include "util/File.h"
#include "util/log.h"
#include "xml/ts_119612v020201_201601xsd.hxx"

DIGIDOCPP_WARNING_PUSH
DIGIDOCPP_WARNING_DISABLE_CLANG("-Wnull-conversion")
DIGIDOCPP_WARNING_DISABLE_GCC("-Wunused-parameter")
DIGIDOCPP_WARNING_DISABLE_MSVC(4005)
#include <xsec/enc/OpenSSL/OpenSSLCryptoX509.hpp>
#include <xsec/enc/XSECKeyInfoResolverDefault.hpp>
#include <xsec/framework/XSECException.hpp>
#include <xsec/framework/XSECProvider.hpp>
DIGIDOCPP_WARNING_POP

#include <cmath>
#include <fstream>
#include <future>

using namespace digidoc;
using namespace digidoc::tsl;
using namespace digidoc::util;
using namespace std;
using namespace xercesc;
using namespace xml_schema;

const set<string_view> TSL::SCHEMES_URI = {
    "http://uri.etsi.org/TrstSvc/eSigDir-1999-93-EC-TrustedList/TSLType/schemes",
    "http://uri.etsi.org/TrstSvc/TrustedList/TSLType/EUlistofthelists",
};

const set<string_view> TSL::GENERIC_URI = {
    "http://uri.etsi.org/TrstSvc/eSigDir-1999-93-EC-TrustedList/TSLType/generic",
    "http://uri.etsi.org/TrstSvc/TSLtype/generic/eSigDir-1999-93-EC-TrustedList",
    "http://uri.etsi.org/TrstSvc/TrustedList/TSLType/EUgeneric",
};

const set<string_view> TSL::SERVICESTATUS_START = {
    "http://uri.etsi.org/TrstSvc/eSigDir-1999-93-EC-TrustedList/Svcstatus/undersupervision",
    //ts_119612v010201
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/undersupervision",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/supervisionincessation",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/accredited",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/setbynationallaw",
    //ts_119612v020201
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/granted",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/recognisedatnationallevel",
};

const set<string_view> TSL::SERVICESTATUS_END = {
    //ts_119612v010201
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/supervisionceased",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/supervisionrevoked",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/accreditationceased",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/accreditationrevoked",
    //ts_119612v020201
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/withdrawn",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/deprecatedatnationallevel",
};

const set<string_view> TSL::SERVICES_SUPPORTED = {
    "http://uri.etsi.org/TrstSvc/Svctype/CA/QC",
    //"http://uri.etsi.org/TrstSvc/Svctype/CA/PKC", //???
    //"http://uri.etsi.org/TrstSvc/Svctype/NationalRootCA-QC", //???
    "http://uri.etsi.org/TrstSvc/Svctype/Certstatus/OCSP",
    "http://uri.etsi.org/TrstSvc/Svctype/Certstatus/OCSP/QC",
    "http://uri.etsi.org/TrstSvc/Svctype/TSA",
    "http://uri.etsi.org/TrstSvc/Svctype/TSA/QTST",
    "http://uri.etsi.org/TrstSvc/Svctype/TSA/TSS-QC", //???
    "http://uri.etsi.org/TrstSvc/Svctype/TSA/TSS-AdESQCandQES", //???
};



TSL::TSL(string file)
    : path(move(file))
{
    try {
        if(!File::fileExists(path))
            return;
        Properties properties;
        properties.schema_location("http://uri.etsi.org/02231/v2#",
            Conf::instance()->xsdPath() + "/ts_119612v020201_201601xsd.xsd");
        tsl = trustServiceStatusList(path,
            Flags::keep_dom|Flags::dont_initialize|Flags::dont_validate, properties);
    }
    catch(const Parsing &e)
    {
        stringstream s;
        s << e;
        WARN("Failed to parse TSL %s %s: %s", territory().c_str(), path.c_str(), s.str().c_str());
    }
    catch(const xsd::cxx::exception &e)
    {
        WARN("Failed to parse TSL %s %s: %s", territory().c_str(), path.c_str(), e.what());
    }
    catch(const XMLException &e)
    {
        try {
            string result = xsd::cxx::xml::transcode<char>(e.getMessage());
            WARN("Failed to parse TSL %s %s: %s", territory().c_str(), path.c_str(), result.c_str());
        } catch(const xsd::cxx::xml::invalid_utf16_string & /* ex */) {
             WARN("Failed to parse TSL %s %s", territory().c_str(), path.c_str());
        }
    }
    catch(const Exception &e)
    {
        WARN("Failed to parse TSL %s %s: %s", territory().c_str(), path.c_str(), e.msg().c_str());
    }
    catch(...)
    {
        WARN("Failed to parse TSL %s %s", territory().c_str(), path.c_str());
    }
}

bool TSL::activate(const string &territory)
{
    if(territory.size() != 2)
        return false;
    string cache = CONF(TSLCache);
    string path = cache + "/" + territory + ".xml";
    if(File::fileExists(path))
        return false;
    ofstream(File::encodeName(path).c_str(), ofstream::binary) << " ";
    return true;
}

vector<TSL::Service> TSL::services() const
{
    if(GENERIC_URI.find(type()) == GENERIC_URI.cend() ||
        !tsl->trustServiceProviderList())
        return {};

    vector<Service> services;
    for(const TSPType &pointer: tsl->trustServiceProviderList()->trustServiceProvider())
    {
        for(const TSPServiceType &service: pointer.tSPServices().tSPService())
        {
            const TSPServiceInformationType &serviceInfo = service.serviceInformation();
            if(SERVICES_SUPPORTED.find(serviceInfo.serviceTypeIdentifier()) == SERVICES_SUPPORTED.cend())
                continue;
            Service s;
            s.type = serviceInfo.serviceTypeIdentifier();
            s.name = toString(serviceInfo.serviceName());
            time_t previousTime = 0;
            if(!parseInfo(serviceInfo, s, previousTime))
                continue;
            if(service.serviceHistory())
            {
                for(const ServiceHistoryInstanceType &history: service.serviceHistory()->serviceHistoryInstance())
                {
                    if(history.serviceTypeIdentifier() != serviceInfo.serviceTypeIdentifier())
                        DEBUG("History service type is not supported %s", history.serviceTypeIdentifier().c_str());
                    else
                        parseInfo(history, s, previousTime);
                }
            }
            services.push_back(move(s));
        }
    }
    return services;
}

void TSL::debugException(const digidoc::Exception &e)
{
    Log::out(Log::DebugType, e.file().c_str(), e.line(), "%s", e.msg().c_str());
    for(const Exception &ex: e.causes())
        debugException(ex);
}

string TSL::fetch(const string &url, const string &path)
{
    try
    {
        Connect::Result r = Connect(url, "GET", CONF(TSLTimeOut)).exec({{"Accept-Encoding", "gzip"}});
        if(!r.isOK() || r.content.empty())
            THROW("HTTP status code is not 200 or content is empty");
        ofstream(File::encodeName(path).c_str(), fstream::binary|fstream::trunc) << r.content;
        return r.headers["ETag"];
    }
    catch(const Exception &)
    {
        ERR("TSL %s Failed to download list", url.c_str());
        throw;
    }
}

bool TSL::isExpired() const
{
    return !tsl || !tsl->schemeInformation().nextUpdate().dateTime() ||
        date::xsd2time_t(tsl->schemeInformation().nextUpdate().dateTime().get()) < time(nullptr);
}

string TSL::issueDate() const
{
    return !tsl ? string() : date::to_string(tsl->schemeInformation().listIssueDateTime());
}

string TSL::nextUpdate() const
{
    return !tsl || !tsl->schemeInformation().nextUpdate().dateTime() ?
        string() : date::to_string(tsl->schemeInformation().nextUpdate().dateTime().get());
}

string TSL::operatorName() const
{
    return !tsl ? string() : toString(tsl->schemeInformation().schemeOperatorName());
}

vector<TSL::Service> TSL::parse()
{
    string url = CONF(TSLUrl);
    string cache = CONF(TSLCache);
    vector<X509Cert> cert = CONF(TSLCerts);
    File::createDirectory(cache);
    return parse(url, cert, cache, File::fileName(url));
}

vector<TSL::Service> TSL::parse(const string &url, const vector<X509Cert> &certs,
    const string &cache, const string &territory)
{
    try {
        TSL tsl = parseTSL(url, certs, cache, territory);
        if(tsl.pointers().empty())
            return tsl.services();

        vector< future< vector<TSL::Service> > > futures;
        for(const TSL::Pointer &p: tsl.pointers())
        {
            if(!File::fileExists(cache + "/" + p.territory + ".xml"))
                continue;
            futures.push_back(async(launch::async, [p, cache]{
                return parse(p.location, p.certs, cache, p.territory + ".xml");
            }));
        }
        vector<Service> list;
        for(auto &f: futures)
        {
            vector<Service> services = f.get();
            list.insert(list.end(), make_move_iterator(services.begin()), make_move_iterator(services.end()));
        }
        return list;
    }
    catch(const Exception &e)
    {
        debugException(e);
        ERR("TSL %s Failed to validate list", territory.c_str());
        return {};
    }
}

TSL TSL::parseTSL(const string &url, const vector<X509Cert> &certs,
    const string &cache, const string &territory)
{
    string path = File::path(cache, territory);
    TSL valid;
    try {
        TSL tsl(path);
        tsl.validate(certs);
        valid = tsl;
        DEBUG("TSL %s (%llu) signature is valid", territory.c_str(), tsl.sequenceNumber());

        if(valid.isExpired())
        {
            if(!CONF(TSLAutoUpdate) && CONF(TSLAllowExpired))
                return valid;
            THROW("TSL %s (%llu) is expired", territory.c_str(), tsl.sequenceNumber());
        }

        if(CONF(TSLOnlineDigest) && (File::modifiedTime(path) < (time(nullptr) - (60 * 60 * 24))))
        {
            if(valid.validateETag(url))
                File::updateModifiedTime(path, time(nullptr));
        }

        return valid;
    } catch(const Exception &) {
        ERR("TSL %s signature is invalid", territory.c_str());
        if(!CONF(TSLAutoUpdate))
            throw;
    }

    try {
        string tmp = path + ".tmp";
        string etag = fetch(url, tmp);
        TSL tsl = TSL(tmp);
        tsl.validate(certs);
        valid = tsl;

        ofstream(File::encodeName(path).c_str(), ofstream::binary|fstream::trunc)
            << ifstream(File::encodeName(tmp).c_str(), fstream::binary).rdbuf();
        File::removeFile(tmp);

        ofstream(File::encodeName(path + ".etag").c_str(), ofstream::trunc) << etag;
        DEBUG("TSL %s (%llu) signature is valid", territory.c_str(), tsl.sequenceNumber());
    } catch(const Exception &) {
        ERR("TSL %s signature is invalid", territory.c_str());
        if(!valid.tsl)
            throw;
    }

    if(valid.isExpired() && !CONF(TSLAllowExpired))
        THROW("TSL %s (%llu) is expired", territory.c_str(), valid.sequenceNumber());

    return valid;
}

template<class Info>
bool TSL::parseInfo(const Info &info, Service &s, time_t &previousTime)
{
    vector<Qualifier> qualifiers;
    if(info.serviceInformationExtensions())
    {
        for(const ExtensionType &extension: info.serviceInformationExtensions()->extension())
        {
            if(extension.critical())
            {
                if(extension.takenOverByType())
                    WARN("Found critical extension TakenOverByType '%s'", toString(extension.takenOverByType()->tSPName()).c_str());
                if(extension.expiredCertsRevocationInfo())
                {
                    WARN("Found critical extension ExpiredCertsRevocationInfo");
                    return false;
                }
            }
            if(extension.additionalServiceInformationType())
                s.additional = extension.additionalServiceInformationType()->uRI();
            if(extension.qualificationsType())
            {
                for(const QualificationElementType &element: extension.qualificationsType()->qualificationElement())
                {
                    Qualifier q;
                    for(const QualifierType &qualifier: element.qualifiers().qualifier())
                    {
                        if(qualifier.uri())
                            q.qualifiers.push_back(qualifier.uri().get());
                    }
                    const CriteriaListType &criteria = element.criteriaList();
                    if(criteria.assert_())
                        q.assert_ = criteria.assert_().get();
                    for(const KeyUsageType &keyUsage: criteria.keyUsage())
                    {
                        map<X509Cert::KeyUsage,bool> usage;
                        for(const KeyUsageBitType &bit: keyUsage.keyUsageBit())
                        {
                            if(!bit.name())
                                continue;
                            if(bit.name().get() == "digitalSignature")
                                usage[X509Cert::DigitalSignature] = bit;
                            if(bit.name().get() == "nonRepudiation")
                                usage[X509Cert::NonRepudiation] = bit;
                            if(bit.name().get() == "keyEncipherment")
                                usage[X509Cert::KeyEncipherment] = bit;
                            if(bit.name().get() == "dataEncipherment")
                                usage[X509Cert::DataEncipherment] = bit;
                            if(bit.name().get() == "keyAgreement")
                                usage[X509Cert::KeyAgreement] = bit;
                            if(bit.name().get() == "keyCertSign")
                                usage[X509Cert::KeyCertificateSign] = bit;
                            if(bit.name().get() == "crlSign")
                                usage[X509Cert::CRLSign] = bit;
                            if(bit.name().get() == "encipherOnly")
                                usage[X509Cert::EncipherOnly] = bit;
                            if(bit.name().get() == "decipherOnly")
                                usage[X509Cert::DecipherOnly] = bit;
                        }
                        q.keyUsage.push_back(move(usage));
                    }
                    for(const PoliciesListType &policySet: criteria.policySet())
                    {
                        vector<string> policies;
                        for(const xades::ObjectIdentifierType &policy: policySet.policyIdentifier())
                            policies.push_back(policy.identifier());
                        q.policySet.push_back(move(policies));
                    }
                    qualifiers.push_back(move(q));
                }
            }
        }
    }

    for(const DigitalIdentityType &id: info.serviceDigitalIdentity().digitalId())
    {
        if(!id.x509Certificate())
            continue;
        const Base64Binary &base64 = id.x509Certificate().get();
        s.certs.emplace_back((const unsigned char*)base64.data(), base64.size());
    }

    if(SERVICESTATUS_START.find(info.serviceStatus()) != SERVICESTATUS_START.cend())
        s.validity.push_back({date::xsd2time_t(info.statusStartingTime()), previousTime, qualifiers});
    else if(SERVICESTATUS_END.find(info.serviceStatus()) == SERVICESTATUS_END.cend())
        DEBUG("Unknown service status %s", info.serviceStatus().c_str());
    previousTime = date::xsd2time_t(info.statusStartingTime());
    return true;
}

vector<string> TSL::pivotURLs() const
{
    if(!tsl)
        return {};
    string current(File::fileName(path));
    size_t pos = current.find_first_of('.');
    if(current.find("pivot") != string::npos && pos != string::npos)
        current.resize(pos);
    vector<string> result;
    for(const auto &uri: tsl->schemeInformation().schemeInformationURI().uRI())
    {
        if(uri.lang() == "en" && uri.find("pivot") != string::npos && uri.find(current) == string::npos)
            result.push_back(uri);
    }
    return result;
}

vector<TSL::Pointer> TSL::pointers() const
{
    if(SCHEMES_URI.find(type()) == SCHEMES_URI.cend() ||
        !tsl->schemeInformation().pointersToOtherTSL())
        return {};
    vector<Pointer> pointer;
    for(const OtherTSLPointersType::OtherTSLPointerType &other:
        tsl->schemeInformation().pointersToOtherTSL()->otherTSLPointer())
    {
        if(!other.additionalInformation() ||
           other.additionalInformation()->mimeType() != "application/vnd.etsi.tsl+xml")
            continue;
        Pointer p;
        p.territory = other.additionalInformation()->schemeTerritory();
        p.location = string(other.tSLLocation());
        p.certs = serviceDigitalIdentities(other, p.territory);
        if(!p.certs.empty())
            pointer.push_back(move(p));
    }
    return pointer;
}

unsigned long long  TSL::sequenceNumber() const
{
    return !tsl ? 0 : tsl->schemeInformation().tSLSequenceNumber();
}

vector<X509Cert> TSL::serviceDigitalIdentities(const tsl::OtherTSLPointerType &other, string_view region)
{
    if(!other.serviceDigitalIdentities())
        return {};
    vector<X509Cert> result;
    for(const auto &service: other.serviceDigitalIdentities()->serviceDigitalIdentity())
    {
        for(const auto &digitalID: service.digitalId())
        {
            if(!digitalID.x509Certificate())
                continue;
            const Base64Binary &base64 = digitalID.x509Certificate().get();
            try {
                result.emplace_back((const unsigned char*)base64.data(), base64.size());
                continue;
            } catch(const Exception &e) {
                DEBUG("Failed to parse %s certificate, Testing also parse as PEM: %s", region.data(), e.msg().c_str());
            }
            try {
                result.emplace_back((const unsigned char*)base64.data(), base64.size(), X509Cert::Pem);
            } catch(const Exception &e) {
                DEBUG("Failed to parse %s certificate as PEM: %s", region.data(), e.msg().c_str());
            }
        }
    }
    return result;
}

X509Cert TSL::signingCert() const
{
    if(!tsl ||
        !tsl->signature() ||
        !tsl->signature()->keyInfo() ||
        tsl->signature()->keyInfo()->x509Data().empty() ||
        tsl->signature()->keyInfo()->x509Data().front().x509Certificate().empty())
        return X509Cert();
    const Base64Binary &base64 = tsl->signature()->keyInfo()->x509Data().front().x509Certificate().front();
    return X509Cert((const unsigned char*)base64.data(), base64.size());
}

vector<X509Cert> TSL::signingCerts() const
{
    if(!tsl || !tsl->schemeInformation().pointersToOtherTSL())
        return {};
    vector<X509Cert> result;
    for(const auto &other: tsl->schemeInformation().pointersToOtherTSL()->otherTSLPointer())
    {
        vector<X509Cert> certs = serviceDigitalIdentities(other, "pivot");
        result.insert(result.cend(), make_move_iterator(certs.begin()), make_move_iterator(certs.end()));
    }
    return result;
}

string TSL::territory() const
{
    return !tsl || !tsl->schemeInformation().schemeTerritory() ?
        string() : tsl->schemeInformation().schemeTerritory().get();
}

string TSL::toString(const InternationalNamesType &obj, string_view lang)
{
    for(const InternationalNamesType::NameType &name: obj.name())
        if(name.lang() == lang)
            return name;
    return obj.name().front();
}

string TSL::type() const
{
    return !tsl ? string() : tsl->schemeInformation().tSLType();
}

string TSL::url() const
{
    if(!tsl)
        return {};
    const TSLSchemeInformationType &info = tsl->schemeInformation();
    if(!info.distributionPoints() || info.distributionPoints().get().uRI().empty())
        return {};
    return info.distributionPoints().get().uRI().front();
}

void TSL::validate(const X509Cert &cert) const
{
    if(!tsl)
        THROW("Failed to parse XML");
    if(!cert)
        THROW("TSL empty signing certificate");

    try {
        XSECProvider prov;
        auto deleteSig = [&](DSIGSignature *s) { prov.releaseSignature(s); };
        unique_ptr<DSIGSignature, decltype(deleteSig)> sig(prov.newSignatureFromDOM(tsl->_node()->getOwnerDocument()), deleteSig);
        sig->setSigningKey(OpenSSLCryptoX509(cert.handle()).clonePublicKey());
        sig->registerIdAttributeName((const XMLCh*)u"ID");
        sig->setIdByAttributeName(true);
        sig->load();
        if(!sig->verify())
        {
            try {
                string result = xsd::cxx::xml::transcode<char>(sig->getErrMsgs());
                THROW("TSL %s Signature is invalid: %s", territory().c_str(), result.c_str());
            } catch(const xsd::cxx::xml::invalid_utf16_string & /* ex */) {
                THROW("TSL %s Signature is invalid", territory().c_str());
            }
        }
    }
    catch(const XSECException &e)
    {
        try {
            string result = xsd::cxx::xml::transcode<char>(e.getMsg());
            THROW("TSL %s Signature is invalid: %s", territory().c_str(), result.c_str());
        } catch(const xsd::cxx::xml::invalid_utf16_string & /* ex */) {
            THROW("TSL %s Signature is invalid", territory().c_str());
        }
    }
    catch(const xsd::cxx::xml::invalid_utf16_string & /* ex */) {
        THROW("Failed to parse XML.");
    }
    catch(const Exception &)
    {
        throw;
    }
    catch(...)
    {
        THROW("TSL %s Signature is invalid", territory().c_str());
    }
}

void TSL::validate(const vector<X509Cert> &certs, int recursion) const
{
    if(recursion > 3)
        THROW("PIVOT TSL recursion parsing limit");
    if(certs.empty())
        THROW("TSL trusted signing certificates empty");
    X509Cert cert = signingCert();
    if(find(certs.cbegin(), certs.cend(), cert) != certs.cend())
    {
        validate(cert);
        return;
    }

    vector<string> urls = pivotURLs();
    if(urls.empty())
        THROW("TSL %s Signature is signed with untrusted certificate", territory().c_str());

    // https://ec.europa.eu/tools/lotl/pivot-lotl-explanation.html
    string path = File::path(CONF(TSLCache), File::fileName(urls[0]));
    TSL pivot(path);
    if(!pivot.tsl)
    {
        string etag = fetch(urls[0], path);
        ofstream(File::encodeName(path + ".etag").c_str(), ofstream::trunc) << etag;
        pivot = TSL(path);
    }
    pivot.validate(certs, recursion + 1);
    validate(pivot.signingCerts(), recursion);
}

/**
 * Check if HTTP ETag header is the same as ETag of the cached TSL
 * @param url Url of the TSL
 * @param timeout Time to wait for downloading
 * @throws Exception if ETag does not match cached ETag and TSL loading should be triggered
 */
bool TSL::validateETag(const string &url)
{
    Connect::Result r;
    try {
        r = Connect(url, "HEAD", CONF(TSLTimeOut)).exec({{"Accept-Encoding", "gzip"}});
        if(!r.isOK())
            return false;
    } catch(const Exception &e) {
        debugException(e);
        DEBUG("Failed to get ETag %s", url.c_str());
        return false;
    }

    map<string,string>::const_iterator it = r.headers.find("ETag");
    if(it == r.headers.cend())
        return validateRemoteDigest(url);

    DEBUG("Remote ETag: %s", it->second.c_str());
    ifstream is(File::encodeName(path + ".etag"));
    if(!is.is_open())
        THROW("Cached ETag does not exist");
    string etag(it->second.size(), 0);
    is.read(etag.data(), streamsize(etag.size()));
    DEBUG("Cached ETag: %s", etag.c_str());
    if(etag != it->second)
        THROW("Remote ETag does not match");
    return true;
}

bool TSL::validateRemoteDigest(const string &url)
{
    size_t pos = url.find_last_of("/.");
    if(pos == string::npos)
        return false;

    Connect::Result r;
    try
    {
        r= Connect(url.substr(0, pos) + ".sha2", "GET", CONF(TSLTimeOut)).exec();
        if(!r.isOK())
            return false;
    } catch(const Exception &e) {
        debugException(e);
        DEBUG("Failed to get remote digest %s", url.c_str());
        return false;
    }

    vector<unsigned char> digest;
    if(r.content.size() == 32)
        digest.assign(r.content.c_str(), r.content.c_str() + r.content.size());
    else
    {
        r.content.erase(r.content.find_last_not_of(" \n\r\t") + 1);
        if(r.content.size() != 64)
            return false;
        digest = File::hexToBin(r.content);
    }

    Digest sha(URI_RSA_SHA256);
    vector<unsigned char> buf(10240, 0);
    ifstream is(path, ifstream::binary);
    while(is)
    {
        is.read((char*)buf.data(), streamsize(buf.size()));
        if(is.gcount() > 0)
            sha.update(buf.data(), size_t(is.gcount()));
    }

    if(!digest.empty() && digest != sha.result())
        THROW("TSL %s remote digest does not match local. TSL might be outdated", territory().c_str());
    return true;
}
