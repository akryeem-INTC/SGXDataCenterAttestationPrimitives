/*
* Copyright (c) 2017, Intel Corporation
*
* Redistribution and use in source and binary forms, with or without modification,
* are permitted provided that the following conditions are met:

* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
* 3. Neither the name of the copyright holder nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
* THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
* OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "PckCrlVerifier.h"

#include <CertVerification/X509Constants.h>
#include <OpensslHelpers/SignatureVerification.h>

namespace intel { namespace sgx { namespace qvl {

PckCrlVerifier::PckCrlVerifier(const CommonVerifier& commonVerifier)
    : _commonVerifier{commonVerifier}
{
}


Status PckCrlVerifier::verify(const pckparser::CrlStore &crl, const pckparser::CertStore &crlIssuer) const
{
    if(crl.getIssuer() != crlIssuer.getSubject())
    {
        return STATUS_SGX_CRL_UNKNOWN_ISSUER;
    }

    if(crl.expired())
    {
        return STATUS_SGX_CRL_INVALID;
    }

    if(!_commonVerifier.checkStandardExtensions(crl.getExtensions(), qvl::constants::CRL_REQUIRED_EXTENSIONS))
    {
        return STATUS_SGX_CRL_INVALID_EXTENSIONS;
    }

    if(!crypto::verifySignature(crl, crlIssuer.getPubKey()))
    {
        return STATUS_SGX_CRL_INVALID_SIGNATURE;
    }

    return STATUS_OK;
}

Status PckCrlVerifier::verify(const pckparser::CrlStore &crl, const CertificateChain &chain, const pckparser::CertStore &trustedRoot) const
{
    const auto chainVerificationStatus = verifyCRLIssuerCertChain(chain, crl);
    if(chainVerificationStatus != STATUS_OK)
    {
        return chainVerificationStatus;
    }

    const auto topmostCert = chain.getTopmostCert();
    if(!topmostCert)
    {
        return STATUS_SGX_CRL_UNKNOWN_ISSUER;
    }
    
    const auto basicVerificationStatus = verify(crl, *topmostCert);
    if(basicVerificationStatus != STATUS_OK)
    {
        return basicVerificationStatus;
    }
     
    if(_commonVerifier.verifyRootCACert(trustedRoot) != STATUS_OK)
    {
        return STATUS_TRUSTED_ROOT_CA_INVALID;
    }

    const auto chainRootCert = chain.getRootCert();
    if(!chainRootCert )
    {
        return STATUS_SGX_ROOT_CA_MISSING;
    }
    if(chainRootCert->getSignature().rawDer != trustedRoot.getSignature().rawDer )
    {
        return STATUS_SGX_ROOT_CA_UNTRUSTED;
    }

    return STATUS_OK;
}

Status PckCrlVerifier::verifyCRLIssuerCertChain(const CertificateChain &chain, const pckparser::CrlStore& crl) const
{
    const auto rootCertFromChain = chain.get(constants::ROOT_CA_SUBJECT);
    if(!rootCertFromChain)
    {
        return STATUS_SGX_ROOT_CA_INVALID;
    }

    if(_commonVerifier.verifyRootCACert(*rootCertFromChain) != STATUS_OK)
    {
        return STATUS_SGX_CA_CERT_INVALID;
    }

    if(crl.getIssuer() == constants::ROOT_CA_CRL_ISSUER)
    {
        if(chain.length() != 1)
        {
            return STATUS_SGX_CA_CERT_UNSUPPORTED_FORMAT;
        }
    }
    else if(crl.getIssuer() == constants::PCK_PLATFORM_CRL_ISSUER || crl.getIssuer() == constants::PCK_PROCESSOR_CRL_ISSUER)
    {
        if(chain.length() != 2)
        {
            return STATUS_SGX_CA_CERT_UNSUPPORTED_FORMAT;
        }

        const auto topmostCert = chain.getTopmostCert();
        if(!topmostCert)
        {
            return STATUS_SGX_INTERMEDIATE_CA_MISSING;
        }

        const auto verifyIntermediateStatus = _commonVerifier.verifyIntermediate(*topmostCert, *rootCertFromChain);
        if(verifyIntermediateStatus != STATUS_OK)
        {
            return STATUS_SGX_CA_CERT_INVALID;
        }
    }
    else
    {
        return STATUS_SGX_CRL_UNKNOWN_ISSUER;
    }

    return STATUS_OK;
}

bool PckCrlVerifier::checkValidityPeriodAndIssuer(const pckparser::CrlStore& crl)
{
    return
        !crl.expired() &&
        (crl.getIssuer() == constants::PCK_PROCESSOR_CRL_ISSUER ||
         crl.getIssuer() == constants::PCK_PLATFORM_CRL_ISSUER);
}

}}}// namespace intel { namespace sgx { namespace qvl {
