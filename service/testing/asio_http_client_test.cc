#define BOOST_TEST_MAIN
#define BOOST_TEST_DYN_LINK

#include <memory>
#include <string>
#include <tuple>
#include <boost/test/unit_test.hpp>

#include "jml/arch/timers.h"
#include "jml/utils/testing/watchdog.h"
#include "soa/service/rest_proxy.h"
#include "soa/service/asio_http_client.h"
#include "soa/utils/print_utils.h"

#include "test_http_services.h"


using namespace std;
using namespace Datacratic;


/* helpers functions used in tests */
namespace {

typedef tuple<HttpClientError, int, string> ClientResponse;

#define CALL_MEMBER_FN(object, pointer)  (object.*(pointer))

/* sync request helpers */
template<typename Func>
ClientResponse
doRequest(boost::asio::io_service & ioService,
          const string & baseUrl, const string & resource,
          Func func,
          const RestParams & queryParams, const RestParams & headers,
          int timeout = -1)
{
    ClientResponse response;

    AsioHttpClient client(ioService, baseUrl, 4);

    bool done(false);
    auto onResponse = [&] (const HttpRequest & rq,
                           HttpClientError error,
                           int status,
                           string && headers,
                           string && body) {
        int & code = get<1>(response);
        code = status;
        string & body_ = get<2>(response);
        body_ = move(body);
        HttpClientError & errorCode = get<0>(response);
        errorCode = error;
        done = true;
        ioService.stop();
    };
    auto cbs = make_shared<HttpClientSimpleCallbacks>(onResponse);

    CALL_MEMBER_FN(client, func)(resource, cbs, queryParams, headers,
                                 timeout);

    ioService.reset();
    ioService.run();
    BOOST_CHECK_EQUAL(done, true);

    return response;
}

ClientResponse
doGetRequest(boost::asio::io_service & ioService,
             const string & baseUrl, const string & resource,
             const RestParams & queryParams = RestParams(),
             const RestParams & headers = RestParams(),
             int timeout = -1)
{
    return doRequest(ioService, baseUrl, resource, &AsioHttpClient::get,
                     queryParams, headers, timeout);
}

ClientResponse
doDeleteRequest(boost::asio::io_service & ioService,
                const string & baseUrl, const string & resource,
                const RestParams & queryParams = RestParams(),
                const RestParams & headers = RestParams(),
                int timeout = -1)
{
    return doRequest(ioService, baseUrl, resource, &AsioHttpClient::del,
                     queryParams, headers, timeout);
}

ClientResponse
doUploadRequest(boost::asio::io_service & ioService,
                bool isPut,
                const string & baseUrl, const string & resource,
                const string & body, const string & type)
{
    ClientResponse response;

    AsioHttpClient client(ioService, baseUrl, 4);

    int done(false);
    auto onResponse = [&] (const HttpRequest & rq,
                           HttpClientError error,
                           int status,
                           string && headers,
                           string && body) {
        int & code = get<1>(response);
        code = status;
        string & body_ = get<2>(response);
        body_ = move(body);
        HttpClientError & errorCode = get<0>(response);
        errorCode = error;
        done = true;
        ioService.stop();
    };

    auto cbs = make_shared<HttpClientSimpleCallbacks>(onResponse);
    HttpRequest::Content content(body, type);
    if (isPut) {
        client.put(resource, cbs, content);
    }
    else {
        client.post(resource, cbs, content);
    }

    ioService.reset();
    ioService.run();
    BOOST_CHECK_EQUAL(done, true);

    return response;
}

}

#if 1
BOOST_AUTO_TEST_CASE( test_http_client_get )
{
    cerr << "client_get\n";
    ML::Watchdog watchdog(10);
    auto proxies = make_shared<ServiceProxies>();
    HttpGetService service(proxies);

    service.addResponse("GET", "/coucou", 200, "coucou");
    service.start();

    boost::asio::io_service ioService;

    service.waitListening();

#if 0
    /* request to bad ip
       Note: if the ip resolution timeout is very high on the router, the
       Watchdog timeout might trigger first */
    {
        ::fprintf(stderr, "request to bad ip\n");
        string baseUrl("http://123.234.12.23");
        auto resp = doGetRequest(ioService, baseUrl, "/");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::CouldNotConnect);
        BOOST_CHECK_EQUAL(get<1>(resp), 0);
    }
#endif

#if 0
    /* request to bad hostname
       Note: will fail when the name service returns a "default" value for all
       non resolved hosts */
    {
        ::fprintf(stderr, "request to bad hostname\n");
        string baseUrl("http://somewhere.lost");
        auto resp = doGetRequest(ioService, baseUrl, "/");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::HostNotFound);
        BOOST_CHECK_EQUAL(get<1>(resp), 0);
    }
#endif

    /* request with timeout */
    {
        ::fprintf(stderr, "request with timeout\n");
        string baseUrl("http://127.0.0.1:" + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/timeout", {}, {}, 1);
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::Timeout);
        BOOST_CHECK_EQUAL(get<1>(resp), 0);
    }

    /* request connection close */
    {
        ::fprintf(stderr, "testing behaviour with connection: close\n");
        string baseUrl("http://127.0.0.1:" + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/connection-close");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
        BOOST_CHECK_EQUAL(get<1>(resp), 204);
    }

    /* request to /nothing -> 404 */
    {
        ::fprintf(stderr, "request with 404\n");
        string baseUrl("http://127.0.0.1:"
                       + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/nothing");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
        BOOST_CHECK_EQUAL(get<1>(resp), 404);
    }

    /* request to /coucou -> 200 + "coucou" */
    {
        ::fprintf(stderr, "request with 200\n");
        string baseUrl("http://127.0.0.1:"
                       + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/coucou");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
        BOOST_CHECK_EQUAL(get<1>(resp), 200);
        BOOST_CHECK_EQUAL(get<2>(resp), "coucou");
    }

    /* headers and cookies */
    {
        string baseUrl("http://127.0.0.1:" + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/headers", {},
                                 {{"someheader", "somevalue"}});
        Json::Value expBody;
        expBody["accept"] = "*/*";
        expBody["host"] = baseUrl.substr(7);
        expBody["someheader"] = "somevalue";
        Json::Value jsonBody = Json::parse(get<2>(resp));
        BOOST_CHECK_EQUAL(jsonBody, expBody);
    }

    /* query-params */
    {
        string baseUrl("http://127.0.0.1:" + to_string(service.port()));
        auto resp = doGetRequest(ioService, baseUrl, "/query-params",
                                 {{"value", "hello"}});
        string body = get<2>(resp);
        BOOST_CHECK_EQUAL(body, "?value=hello");
    }

    service.shutdown();
}
#endif

#if 1
BOOST_AUTO_TEST_CASE( test_http_client_post )
{
    cerr << "client_post\n";
    ML::Watchdog watchdog(10);
    auto proxies = make_shared<ServiceProxies>();
    HttpUploadService service(proxies);
    service.start();

    boost::asio::io_service ioService;

    /* request to /coucou -> 200 + "coucou" */
    {
        string baseUrl("http://127.0.0.1:"
                       + to_string(service.port()));
        auto resp = doUploadRequest(ioService, false, baseUrl, "/post-test",
                                    "post body", "application/x-nothing");
        BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
        BOOST_CHECK_EQUAL(get<1>(resp), 200);
        Json::Value jsonBody = Json::parse(get<2>(resp));
        BOOST_CHECK_EQUAL(jsonBody["verb"], "POST");
        BOOST_CHECK_EQUAL(jsonBody["payload"], "post body");
        BOOST_CHECK_EQUAL(jsonBody["type"], "application/x-nothing");
    }

    service.shutdown();
}
#endif

#if 1
BOOST_AUTO_TEST_CASE( test_http_client_put )
{
    cerr << "client_put\n";
    ML::Watchdog watchdog(10);
    auto proxies = make_shared<ServiceProxies>();
    HttpUploadService service(proxies);
    service.start();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:"
                   + to_string(service.port()));
    string bigBody;
    for (int i = 0; i < 65535; i++) {
        bigBody += "this is one big body,";
    }
    auto resp = doUploadRequest(ioService, true, baseUrl, "/put-test",
                                bigBody, "application/x-nothing");
    BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
    BOOST_CHECK_EQUAL(get<1>(resp), 200);
    Json::Value jsonBody = Json::parse(get<2>(resp));
    BOOST_CHECK_EQUAL(jsonBody["verb"], "PUT");
    BOOST_CHECK_EQUAL(jsonBody["payload"], bigBody);
    BOOST_CHECK_EQUAL(jsonBody["type"], "application/x-nothing");

    service.shutdown();
}
#endif

#if 1
BOOST_AUTO_TEST_CASE( http_test_client_delete )
{
    cerr << "client_delete" << endl;
    ML::Watchdog watchdog(10);

    auto proxies = make_shared<ServiceProxies>();
    HttpGetService service(proxies);

    service.addResponse("DELETE", "/deleteMe", 200, "Deleted");
    service.start();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:" + to_string(service.port()));
    auto resp = doDeleteRequest(ioService, baseUrl, "/deleteMe", {}, {}, 1);

    BOOST_CHECK_EQUAL(get<0>(resp), HttpClientError::None);
    BOOST_CHECK_EQUAL(get<1>(resp), 200);

    service.shutdown();
}
#endif

#if 1
BOOST_AUTO_TEST_CASE( test_http_client_put_multi )
{
    cerr << "client_put_multi\n";
    auto proxies = make_shared<ServiceProxies>();
    HttpUploadService service(proxies);
    service.start();

    string baseUrl("http://127.0.0.1:"
                   + to_string(service.port()));

    boost::asio::io_service ioService;

    AsioHttpClient client(ioService, baseUrl);

    size_t maxRequests(500);
    int done(0);

    auto makeBody = [&] (size_t i) {
        int multiplier = (i < maxRequests / 2) ? -2 : 2;
        size_t bodySize = 2000 + multiplier * i;
        string body = ML::format("%.4x", bodySize);
        size_t rndSize = bodySize - body.size();
        body += randomString(rndSize);

        return body;
    };

    for (size_t i = 0; i < maxRequests; i++) {
        auto sendBody = makeBody(i);
        auto onResponse = [&, sendBody] (const HttpRequest & rq,
                                         HttpClientError error,
                                         int status,
                                         string && headers,
                                         string && body) {
            BOOST_CHECK_EQUAL(error, HttpClientError::None);
            BOOST_CHECK_EQUAL(status, 200);
            Json::Value jsonBody = Json::parse(body);
            BOOST_CHECK_EQUAL(jsonBody["verb"], "PUT");
            BOOST_CHECK_EQUAL(jsonBody["payload"], sendBody);
            BOOST_CHECK_EQUAL(jsonBody["type"], "text/plain");
            done++;
            if (done == maxRequests) {
                ioService.stop();
            }
        };

        auto cbs = make_shared<HttpClientSimpleCallbacks>(onResponse);
        HttpRequest::Content content(sendBody, "text/plain");
        while (!client.put("/", cbs, content)) {
            ML::sleep(0.2);
        }
    };

    ioService.run();
    BOOST_CHECK_EQUAL(done, maxRequests);

    service.shutdown();
}
#endif

#if 1
/* Ensures that all requests are correctly performed under load, including
   when "Connection: close" is encountered once in a while.
   Not a performance test. */
BOOST_AUTO_TEST_CASE( test_http_client_stress_test )
{
    cerr << "stress_test\n";
    // const int mask = 0x3ff; /* mask to use for displaying counts */
    // ML::Watchdog watchdog(300);
    auto proxies = make_shared<ServiceProxies>();
    auto doStressTest = [&] (int numParallel) {
        ::fprintf(stderr, "stress test with %d parallel connections\n",
                  numParallel);

        HttpGetService service(proxies);
        service.start();
        service.waitListening();

        string baseUrl("http://127.0.0.1:"
                       + to_string(service.port()));

        boost::asio::io_service ioService;
        AsioHttpClient client(ioService, baseUrl, numParallel);
        
        int maxReqs(30000), numReqs(0), missedReqs(0);
        int numResponses(0);

        auto onDone = [&] (const HttpRequest & rq,
                           HttpClientError errorCode, int status,
                           string && headers, string && body) {
            numResponses++;

            BOOST_CHECK_EQUAL(errorCode, HttpClientError::None);
            BOOST_CHECK_EQUAL(status, 200);

            int bodyNbr;
            try {
                bodyNbr = stoi(body);
            }
            catch (...) {
                ::fprintf(stderr, "exception when parsing body: %s\n",
                          body.c_str());
                throw;
            }

            int lowerLimit = std::max(0, (numResponses - numParallel));
            int upperLimit = std::min(maxReqs, (numResponses + numParallel));
            if (bodyNbr < lowerLimit || bodyNbr > upperLimit) {
                throw ML::Exception("number of returned server requests "
                                    " is anomalous: %d is out of range"
                                    " [%d,*%d,%d]",
                                    bodyNbr, lowerLimit,
                                    numResponses, upperLimit);
            }

            if (numResponses == numReqs) {
                ioService.stop();
            }
        };

        ioService.reset();
        while (numReqs < maxReqs) {
            const char * url = "/counter";
            auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
            if (client.get(url, cbs)) {
                numReqs++;
                // if ((numReqs & mask) == 0 || numReqs == maxReqs) {
                //     ::fprintf(stderr, "performed %d requests\n", numReqs);
                // }
            }
            else {
                missedReqs++;
            }
        }

        ::fprintf(stderr, "all requests performed, awaiting responses...\n");
        ioService.reset();
        ioService.run();
        BOOST_CHECK_EQUAL(numResponses, numReqs);

        ::fprintf(stderr, "performed %d requests; missed: %d\n",
                  maxReqs, missedReqs);

        service.shutdown();
    };

    doStressTest(1);
    doStressTest(8);
    doStressTest(128);
}
#endif

#if 1
/* Ensure that the move constructor and assignment operator behave
   reasonably well. */
BOOST_AUTO_TEST_CASE( test_http_client_move_constructor )
{
    cerr << "move_constructor\n";
    ML::Watchdog watchdog(30);
    auto proxies = make_shared<ServiceProxies>();

    HttpGetService service(proxies);
    service.addResponse("GET", "/", 200, "coucou");
    service.start();
    service.waitListening();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:"
                   + to_string(service.port()));

    auto doGet = [&] (AsioHttpClient & getClient) {
        int done(false);

        auto onDone = [&] (const HttpRequest & rq,
                           HttpClientError errorCode, int status,
                           string && headers, string && body) {
            done = true;
            ioService.stop();
        };
        auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);

        getClient.get("/", cbs);

        ioService.reset();
        ioService.run();
        BOOST_CHECK_EQUAL(done, true);
    };

    #if 0
    /* move constructor */
    cerr << "testing move constructor\n";
    auto makeClient = [&] () {
        return AsioHttpClient(ioService, baseUrl, 1);
    };
    AsioHttpClient client1(move(makeClient()));
    doGet(client1);

    /* move assignment operator */
    cerr << "testing move assignment op.\n";
    AsioHttpClient client2(ioService, "http://nowhere", 1);
    client2 = move(client1);
    doGet(client2);
#endif
    
    service.shutdown();
}
#endif

#if 1
/* Ensure that an infinite number of requests can be queued when queue size is
 * 0, even from within callbacks. */
BOOST_AUTO_TEST_CASE( test_http_client_unlimited_queue )
{
    static const int maxLevel(4);

    ML::Watchdog watchdog(30);
    auto proxies = make_shared<ServiceProxies>();

    HttpGetService service(proxies);
    service.addResponse("GET", "/", 200, "coucou");
    service.start();
    service.waitListening();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:"
                   + to_string(service.port()));

    AsioHttpClient client(ioService, baseUrl, 4, 0);

    atomic<int> pending(0);
    int done(0);

    function<void(int)> doGet = [&] (int level) {
        pending++;
        auto onDone = [&,level] (const HttpRequest & rq,
                                 HttpClientError errorCode, int status,
                                 string && headers, string && body) {
            if (level < maxLevel) {
                for (int i = 0; i < 10; i++) {
                    doGet(level+1);
                }
            }
            pending--;
            done++;
            if (pending == 0) {
                ioService.stop();
            }
        };
        auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
        client.get("/", cbs);
    };

    doGet(0);

    ioService.reset();
    ioService.run();
    BOOST_CHECK_EQUAL(pending, 0);

    service.shutdown();
}
#endif

#if 1
/* Test connection restoration after a timeout occurs. */
BOOST_AUTO_TEST_CASE( test_http_client_connection_timeout )
{
    ML::Watchdog watchdog(30);
    auto proxies = make_shared<ServiceProxies>();

    HttpGetService service(proxies);
    service.addResponse("GET", "/", 200, "coucou");
    service.start();
    service.waitListening();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:" + to_string(service.port()));

    AsioHttpClient client(ioService, baseUrl, 1);
    client.enableDebug(true);

    int done(0);
    auto onDone = [&] (const HttpRequest & rq,
                       HttpClientError errorCode, int status,
                       string && headers, string && body) {
        done++;
        if (done == 2) {
            ioService.stop();
        }
    };
    auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
    client.get("/timeout", cbs, {}, {}, 1);
    client.get("/", cbs, {}, {}, 1);

    ioService.run();
    BOOST_CHECK_EQUAL(done, 2);

    service.shutdown();
}
#endif

#if 1
/* Test connection restoration after the server closes the connection, under
 * various circumstances. */
BOOST_AUTO_TEST_CASE( test_http_client_connection_closed )
{
    ML::Watchdog watchdog(30);
    auto proxies = make_shared<ServiceProxies>();

    HttpGetService service(proxies);
    service.addResponse("GET", "/", 200, "coucou");
    service.start();
    service.waitListening();

    boost::asio::io_service ioService;

    string baseUrl("http://127.0.0.1:" + to_string(service.port()));

    /* response sent, "Connection: close" header */
    {
        cerr << "* connection-close\n";
        AsioHttpClient client(ioService, baseUrl, 1);
        
        int done(0);
        auto onDone = [&] (const HttpRequest & rq,
                           HttpClientError errorCode, int status,
                           string && headers, string && body) {
            done++;
            if (done == 2) {
                ioService.stop();
            }
        };
        auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
        client.get("/connection-close", cbs);
        client.get("/", cbs);

        ioService.reset();
        ioService.run();
        BOOST_CHECK_EQUAL(done, 2);
    }

    /* response sent, no "Connection: close" header */
    {
        cerr << "* no connection-close\n";
        AsioHttpClient client(ioService, baseUrl, 1);
        
        int done(0);
        auto onDone = [&] (const HttpRequest & rq,
                           HttpClientError errorCode, int status,
                           string && headers, string && body) {
            done++;
            if (done == 2) {
                ioService.stop();
            }
        };
        auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
        client.get("/quiet-connection-close", cbs);
        client.get("/", cbs);

        ioService.reset();
        ioService.run();
        BOOST_CHECK_EQUAL(done, 2);
    }

    /* response not sent */
    {
        cerr << "* no response at all\n";
        AsioHttpClient client(ioService, baseUrl, 1);
        
        int done(0);
        auto onDone = [&] (const HttpRequest & rq,
                           HttpClientError errorCode, int status,
                           string && headers, string && body) {
            done++;
            if (done == 2) {
                ioService.stop();
            }
        };
        auto cbs = make_shared<HttpClientSimpleCallbacks>(onDone);
        client.get("/abrupt-connection-close", cbs);
        client.get("/", cbs);

        ioService.reset();
        ioService.run();
        BOOST_CHECK_EQUAL(done, 2);
    }

    service.shutdown();
}
#endif