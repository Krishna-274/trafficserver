/** @file

  Unit test for the X509 Hostname validation functionality

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

#include "tscore/ink_apidefs.h"
#include "tscore/Diags.h"
#include "tscore/ink_resource.h"
#include "tscore/ink_queue.h"
#include "tscore/X509HostnameValidator.h"

#include "tscpp/util/PostScript.h"

// clang-format off

// A simple certificate for CN=test.sslheaders.trafficserver.apache.org.
static const char *test_certificate_cn_name = "test.sslheaders.trafficserver.apache.org";
static const char *test_certificate_cn =
"-----BEGIN CERTIFICATE-----\n"
"MIICGzCCAYSgAwIBAgIJAN/JvtOlj/5HMA0GCSqGSIb3DQEBBQUAMDMxMTAvBgNV\n"
"BAMMKHRlc3Quc3NsaGVhZGVycy50cmFmZmljc2VydmVyLmFwYWNoZS5vcmcwHhcN\n"
"MTQwNzIzMTc1MTA4WhcNMTcwNTEyMTc1MTA4WjAzMTEwLwYDVQQDDCh0ZXN0LnNz\n"
"bGhlYWRlcnMudHJhZmZpY3NlcnZlci5hcGFjaGUub3JnMIGfMA0GCSqGSIb3DQEB\n"
"AQUAA4GNADCBiQKBgQDNuincV56iMA1E7Ss9BlNvRmUdV3An5S6vXHP/hXSVTSj+\n"
"3o0I7es/2noBM7UmXWTBGNjcQYzBed/QIvqM9p5Q4B7kKFTb1xBOl4EU3LHl9fzz\n"
"hxbZMAc2MHW5X8+eCR6K6IBu5sRuLTPvIZhg63/ffhNJTImyW2+eH8guVGd38QID\n"
"AQABozcwNTAzBgNVHREELDAqgih0ZXN0LnNzbGhlYWRlcnMudHJhZmZpY3NlcnZl\n"
"ci5hcGFjaGUub3JnMA0GCSqGSIb3DQEBBQUAA4GBACayHRw5e0iejNkigLARk9aR\n"
"Wiy0WFkUdffhywjnOKxEGvfZGkNQPFN+0SHk7rAm8SlztOIElSvx/y9DByn4IeSw\n"
"2aU6zZiZUSPi9Stg8/tWv9MvOSU/J7CHaHkWuYbfBTBNDokfqFtqY3UJ7Pn+6ybS\n"
"2RZzwmSjinT8GglE30JR\n"
"-----END CERTIFICATE-----\n";

// A completely wildcard certificate with invalid wildcard format SANs- shouldn't match anything
static const char *test_certificate_bad_sans =
"-----BEGIN CERTIFICATE-----\n"
"MIIB7jCCAZigAwIBAgIJAIECheWAKHNWMA0GCSqGSIb3DQEBCwUAMFcxCzAJBgNV\n"
"BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
"aWRnaXRzIFB0eSBMdGQxEDAOBgNVBAMMByouKi4qLiowHhcNMTUwMzA4MTcxOTIy\n"
"WhcNMjUwMzA1MTcxOTIyWjBXMQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1T\n"
"dGF0ZTEhMB8GA1UECgwYSW50ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMRAwDgYDVQQD\n"
"DAcqLiouKi4qMFwwDQYJKoZIhvcNAQEBBQADSwAwSAJBAMeiIvB0e2s7gXc4uxmD\n"
"FeUPjVhjGaGejdkgNoAV/z1sV36G06VGj3JBGkw63fhixVoSfk4MJ/tvuMlu/9E4\n"
"wL0CAwEAAaNHMEUwCQYDVR0TBAIwADALBgNVHQ8EBAMCBeAwKwYDVR0RBCQwIoIB\n"
"KoIFKi5jb22CCioubG9uZ2xvbmeHBMCoAQGHBMCoRQ4wDQYJKoZIhvcNAQELBQAD\n"
"QQC+zaPBEbJhL/Euaf2slgTMTKhnI3DUo/H5WXj54BKpefv0dtzjPD9rpEPqilhO\n"
"w0LiMuz7rapF/2++9BVPPmBh\n"
"-----END CERTIFICATE-----\n";

/* Multiple wildcard SANs:
 * DNS:*.something.or.other, DNS:*.trafficserver.org, DNS:foo*.trafficserver.com, DNS:*bar.trafficserver.net
 * CN: test.sslheaders.trafficserver.apache.org
 */
static const char *test_certificate_cn_and_SANs =
"-----BEGIN CERTIFICATE-----\n"
"MIICajCCAhSgAwIBAgIJAK5xL+HYV+IuMA0GCSqGSIb3DQEBCwUAMHgxCzAJBgNV\n"
"BAYTAkFVMRMwEQYDVQQIDApTb21lLVN0YXRlMSEwHwYDVQQKDBhJbnRlcm5ldCBX\n"
"aWRnaXRzIFB0eSBMdGQxMTAvBgNVBAMMKHRlc3Quc3NsaGVhZGVycy50cmFmZmlj\n"
"c2VydmVyLmFwYWNoZS5vcmcwHhcNMTUwMzI0MTUyNTEwWhcNMjUwMzIxMTUyNTEw\n"
"WjB4MQswCQYDVQQGEwJBVTETMBEGA1UECAwKU29tZS1TdGF0ZTEhMB8GA1UECgwY\n"
"SW50ZXJuZXQgV2lkZ2l0cyBQdHkgTHRkMTEwLwYDVQQDDCh0ZXN0LnNzbGhlYWRl\n"
"cnMudHJhZmZpY3NlcnZlci5hcGFjaGUub3JnMFwwDQYJKoZIhvcNAQEBBQADSwAw\n"
"SAJBAMeiIvB0e2s7gXc4uxmDFeUPjVhjGaGejdkgNoAV/z1sV36G06VGj3JBGkw6\n"
"3fhixVoSfk4MJ/tvuMlu/9E4wL0CAwEAAaOBgDB+MAkGA1UdEwQCMAAwCwYDVR0P\n"
"BAQDAgXgMGQGA1UdEQRdMFuCFCouc29tZXRoaW5nLm9yLm90aGVyghMqLnRyYWZm\n"
"aWNzZXJ2ZXIub3JnghZmb28qLnRyYWZmaWNzZXJ2ZXIuY29tghYqYmFyLnRyYWZm\n"
"aWNzZXJ2ZXIubmV0MA0GCSqGSIb3DQEBCwUAA0EAQmmFmlZQ6lPudkmjJ0K1mSld\n"
"gQP8uiG6cly7NruPZn2Yc1Cha0TycSYfVkRi0dMF2RKtaVvd4uaXDNb4Qpwv3Q==\n"
"-----END CERTIFICATE-----\n";

// clang-format on

static X509 *
load_cert_from_string(const char *cert_string)
{
  BIO *bio = BIO_new_mem_buf((void *)cert_string, -1);
  ts::PostScript bio_defer([&]() -> void { BIO_free(bio); });

  return PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
}

TEST_CASE("CN_match", "[libts][X509HostnameValidator]")
{
  char *matching;
  X509 *x = load_cert_from_string(test_certificate_cn);
  ts::PostScript x_defer([&]() -> void { X509_free(x); });

  REQUIRE(x != nullptr);
  REQUIRE(validate_hostname(x, (unsigned char *)test_certificate_cn_name, false, &matching) == true);
  REQUIRE(strcmp(test_certificate_cn_name, matching) == 0);
  REQUIRE(validate_hostname(x, (unsigned char *)test_certificate_cn_name + 1, false, nullptr) == false);
  ats_free(matching);
}

TEST_CASE("bad_wildcard_SANs", "[libts][X509HostnameValidator]")
{
  X509 *x = load_cert_from_string(test_certificate_bad_sans);
  ts::PostScript x_defer([&]() -> void { X509_free(x); });

  REQUIRE(x != nullptr);
  REQUIRE(validate_hostname(x, (unsigned char *)"something.or.other", false, nullptr) == false);
  REQUIRE(validate_hostname(x, (unsigned char *)"a.b.c", false, nullptr) == false);
  REQUIRE(validate_hostname(x, (unsigned char *)"0.0.0.0", true, nullptr) == false);
  REQUIRE(validate_hostname(x, (unsigned char *)"......", true, nullptr) == false);
  REQUIRE(validate_hostname(x, (unsigned char *)"a.b", true, nullptr) == false);
}

TEST_CASE("wildcard_SAN_and_CN", "[libts][X509HostnameValidator]")
{
  char *matching;
  X509 *x = load_cert_from_string(test_certificate_cn_and_SANs);
  ts::PostScript x_defer([&]() -> void { X509_free(x); });

  REQUIRE(x != nullptr);
  REQUIRE(validate_hostname(x, (unsigned char *)test_certificate_cn_name, false, &matching) == true);
  REQUIRE(strcmp(test_certificate_cn_name, matching) == 0);
  ats_free(matching);

  REQUIRE(validate_hostname(x, (unsigned char *)"a.trafficserver.org", false, &matching) == true);
  REQUIRE(strcmp("*.trafficserver.org", matching) == 0);

  REQUIRE(validate_hostname(x, (unsigned char *)"a.*.trafficserver.org", false, nullptr) == false);
  ats_free(matching);
}

TEST_CASE("IDNA_hostnames", "[libts][X509HostnameValidator]")
{
  char *matching;
  X509 *x = load_cert_from_string(test_certificate_cn_and_SANs);
  ts::PostScript x_defer([&]() -> void { X509_free(x); });

  REQUIRE(x != nullptr);
  REQUIRE(validate_hostname(x, (unsigned char *)"xn--foobar.trafficserver.org", false, &matching) == true);
  REQUIRE(strcmp("*.trafficserver.org", matching) == 0);
  ats_free(matching);

  // IDNA means wildcard must match full label
  REQUIRE(validate_hostname(x, (unsigned char *)"xn--foobar.trafficserver.net", false, &matching) == false);
}

TEST_CASE("middle_label_match", "[libts][X509HostnameValidator]")
{
  char *matching;
  X509 *x = load_cert_from_string(test_certificate_cn_and_SANs);
  ts::PostScript x_defer([&]() -> void { X509_free(x); });

  REQUIRE(x != nullptr);
  REQUIRE(validate_hostname(x, (unsigned char *)"foosomething.trafficserver.com", false, &matching) == true);
  REQUIRE(strcmp("foo*.trafficserver.com", matching) == 0);
  ats_free(matching);
  REQUIRE(validate_hostname(x, (unsigned char *)"somethingbar.trafficserver.net", false, &matching) == true);
  REQUIRE(strcmp("*bar.trafficserver.net", matching) == 0);
  ats_free(matching);

  REQUIRE(validate_hostname(x, (unsigned char *)"a.bar.trafficserver.net", false, nullptr) == false);
  REQUIRE(validate_hostname(x, (unsigned char *)"foo.bar.trafficserver.net", false, nullptr) == false);
}

int
main(int argc, const char **argv)
{
  BaseLogFile *blf = new BaseLogFile("stdout");
  DiagsPtr::set(new Diags("test_x509", nullptr, nullptr, blf));
  res_track_memory = 1;

  SSL_library_init();
  ink_freelists_snap_baseline();

  int status = Catch::Session().run();
  ink_freelists_dump(stdout);

  return status;
}
