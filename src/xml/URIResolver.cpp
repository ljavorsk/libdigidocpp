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

#include "URIResolver.h"

#include "ASiC_E.h"
#include "DataFile_p.h"
#include "util/File.h"

DIGIDOCPP_WARNING_PUSH
DIGIDOCPP_WARNING_DISABLE_MSVC(4005)
#include <xercesc/util/BinInputStream.hpp>
#include <xsec/framework/XSECException.hpp>
DIGIDOCPP_WARNING_POP

#define XSD_CXX11
#include <xsd/cxx/xml/string.hxx>

#include <istream>

using namespace std;
using namespace xercesc;
using namespace digidoc;
using namespace digidoc::util;

class IStreamInputStream: public BinInputStream
{
public:
    explicit IStreamInputStream(istream *is): is_(is)
    {
        is_->clear();
        is_->seekg(0);
    }

    XMLFilePos curPos() const override
    {
        return XMLFilePos(is_->tellg());
    }

    XMLSize_t readBytes(XMLByte * const toFill, const XMLSize_t maxToRead) override
    {
        is_->read((char*)toFill, streamsize(maxToRead));
        return XMLSize_t(is_->gcount());
    }

    const XMLCh *getContentType() const override
    {
        return nullptr;
    }

    istream *is_;
};

URIResolver::URIResolver(ASiContainer *doc): doc_(doc) {}

BinInputStream* URIResolver::resolveURI(const XMLCh *uri)
{
    if(!uri)
        throw XSECException(XSECException::ErrorOpeningURI,
            "XSECURIResolverXerces - anonymous references not supported in default URI Resolvers");

    string _uri = File::fromUriPath(xsd::cxx::xml::transcode<char>(uri));
    if(_uri.front() == '/')
        _uri.erase(0);
    for(const DataFile *file: doc_->dataFiles())
    {
        if(file->fileName() == _uri)
            return new IStreamInputStream(static_cast<const DataFilePrivate*>(file)->m_is.get());
    }

    if(doc_->mediaType() == ASiC_E::MIMETYPE_ADOC)
    {
        auto *adoc = static_cast<ASiC_E*>(doc_);
        for(const DataFile *file: adoc->metaFiles())
        {
            if(file->fileName() == _uri)
                return new IStreamInputStream(static_cast<const DataFilePrivate*>(file)->m_is.get());
        }
    }

    return XSECURIResolverXerces::resolveURI(uri);
}

XSECURIResolver* URIResolver::clone()
{
    return new URIResolver(doc_);
}
