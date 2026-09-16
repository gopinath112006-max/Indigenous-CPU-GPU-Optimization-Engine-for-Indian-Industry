#pragma once

#include "branch_and_bound.hpp"
#include <vector>
#include <queue>
#include <functional>

namespace hypernova::milp {

class NodeSelector {
public:
    virtual ~NodeSelector() = default;
    virtual BnBNode* select_node(std::vector<BnBNode*>& candidates) = 0;
    virtual void add_node(BnBNode* node) = 0;
    virtual bool empty() const = 0;
    virtual std::size_t size() const = 0;
};

class BestFirstSelector : public NodeSelector {
public:
    BnBNode* select_node(std::vector<BnBNode*>& candidates) override;
    void add_node(BnBNode* node) override;
    bool empty() const override { return queue_.empty(); }
    std::size_t size() const override { return queue_.size(); }

private:
    struct Compare {
        bool operator()(const BnBNode* a, const BnBNode* b) const {
            return a->lower_bound > b->lower_bound;
        }
    };
    std::priority_queue<BnBNode*, std::vector<BnBNode*>, Compare> queue_;
};

class DepthFirstSelector : public NodeSelector {
public:
    BnBNode* select_node(std::vector<BnBNode*>& candidates) override;
    void add_node(BnBNode* node) override;
    bool empty() const override { return stack_.empty(); }
    std::size_t size() const override { return stack_.size(); }

private:
    std::vector<BnBNode*> stack_;
};

class BestEstimateSelector : public NodeSelector {
public:
    BnBNode* select_node(std::vector<BnBNode*>& candidates) override;
    void add_node(BnBNode* node) override;
    void set_incumbent(double incumbent) { incumbent_ = incumbent; }
    bool empty() const override { return queue_.empty(); }
    std::size_t size() const override { return queue_.size(); }

private:
    struct Compare {
        double incumbent;
        Compare() : incumbent(0.0) {}
        Compare(double inc) : incumbent(inc) {}
        bool operator()(const BnBNode* a, const BnBNode* b) const {
            double est_a = a->lower_bound + (incumbent - a->lower_bound) * 0.5;
            double est_b = b->lower_bound + (incumbent - b->lower_bound) * 0.5;
            return est_a > est_b;
        }
    };
    double incumbent_ = std::numeric_limits<double>::infinity();
    std::priority_queue<BnBNode*, std::vector<BnBNode*>, Compare> queue_;
};

class HybridSelector : public NodeSelector {
public:
    HybridSelector(int dive_depth = 5);
    BnBNode* select_node(std::vector<BnBNode*>& candidates) override;
    void add_node(BnBNode* node) override;
    void set_incumbent(double incumbent);
    bool empty() const override;
    std::size_t size() const override;

private:
    enum class Phase { DIVE, BEST_FIRST, BEST_ESTIMATE };
    Phase current_phase_ = Phase::DIVE;
    int dive_depth_;
    int dive_count_ = 0;
    double incumbent_ = std::numeric_limits<double>::infinity();
    std::vector<BnBNode*> dive_stack_;
    std::priority_queue<BnBNode*, std::vector<BnBNode*>,
        std::function<bool(BnBNode*, BnBNode*)>> best_first_queue_;
    std::priority_queue<BnBNode*, std::vector<BnBNode*>,
        std::function<bool(BnBNode*, BnBNode*)>> best_estimate_queue_;
};

std::unique_ptr<NodeSelector> create_selector(NodeSelectionStrategy strategy, int dive_depth = 5);

} // namespace hypernova::milp