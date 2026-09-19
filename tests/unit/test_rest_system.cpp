#include <gtest/gtest.h>
#include <rest/rest_server.hpp>

using namespace hypernova;
using namespace hypernova::rest;

TEST(RESTJobSystemTest, SubmitAndRetrieveJob) {
    nlohmann::json prob_json = {
        {"name", "rest_lp_test"},
        {"obj_sense", "MINIMIZE"},
        {"variables", nlohmann::json::array({
            {{"name", "x1"}, {"lower_bound", 0.0}, {"upper_bound", 10.0}, {"objective_coeff", 2.0}},
            {{"name", "x2"}, {"lower_bound", 0.0}, {"upper_bound", 10.0}, {"objective_coeff", 3.0}}
        })},
        {"constraints", nlohmann::json::array({
            {{"name", "c1"}, {"sense", "GE"}, {"rhs", 5.0}}
        })},
        {"matrix", {
            {"rows", 1},
            {"cols", 2},
            {"row_ptr", std::vector<size_t>{0, 2}},
            {"col_indices", std::vector<size_t>{0, 1}},
            {"values", std::vector<double>{1.0, 1.0}}
        }}
    };

    auto& sys = RESTJobSystem::instance();
    std::string job_id = sys.submit_job(prob_json);

    EXPECT_FALSE(job_id.empty());

    // Wait up to 5 seconds for async execution
    nlohmann::json status_json;
    for (int i = 0; i < 50; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        status_json = sys.get_job_status(job_id);
        if (status_json["status"] != "RUNNING" && status_json["status"] != "QUEUED") {
            break;
        }
    }

    EXPECT_EQ(status_json["job_id"], job_id);
    EXPECT_EQ(status_json["name"], "rest_lp_test");
    EXPECT_EQ(status_json["status"], "OPTIMAL");
    EXPECT_NEAR(status_json["objective_value"].get<double>(), 10.0, 1e-4);
}
