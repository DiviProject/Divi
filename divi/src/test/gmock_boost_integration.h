#ifndef GMOCK_BOOST_INTEGRATION_H
#define GMOCK_BOOST_INTEGRATION_H

#include <gmock/gmock.h>
#include <boost/test/unit_test.hpp>

class HookupListner : public ::testing::EmptyTestEventListener                    
{                                                                                 
public:                                                                           
    void OnTestPartResult(const ::testing::TestPartResult& result)                
    {                                                                             
        boost::unit_test::unit_test_log                                           
            << boost::unit_test::log::begin(result.file_name(), result.line_number())
            << boost::unit_test::log_all_errors                                   
            << result.summary()                                                   
            << boost::unit_test::log::end();                                      
        boost::unit_test::framework::assertion_result(                            
            result.passed() ? boost::unit_test::AR_PASSED : boost::unit_test::AR_FAILED);
    }                                                                             
};

struct Fixture                                                                    
{                                                                                 
    Fixture()                                                                     
    {                                                                             
        auto& suite{ boost::unit_test::framework::master_test_suite() };          
        ::testing::InitGoogleMock(&suite.argc, suite.argv);

        // hook up the gmock and boost test                                       
        auto& listeners{ ::testing::UnitTest::GetInstance()->listeners() };       
        delete listeners.Release(listeners.default_result_printer());             
        listeners.Append(new HookupListner);                                      
    }                                                                             

    ~Fixture()                                                                    
    {                                                                             
    }                                                                             
};
BOOST_GLOBAL_FIXTURE(Fixture);

#endif // GMOCK_BOOST_INTEGRATION_H