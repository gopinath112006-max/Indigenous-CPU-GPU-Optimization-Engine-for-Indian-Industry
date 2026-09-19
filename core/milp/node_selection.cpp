#include "node_selection.hpp"
#include <algorithm>
#include <limits>

namespace hypernova::milp {

BnBNode* BestFirstSelector::select_node(std::vector<BnBNode*>& candidates) {
    for (auto* node : candidates) {
        queue_.push(node);
    }
    candidates.clear();

    while (!queue_.empty()) {
        BnBNode* node = queue_.top();
        queue_.pop();
        if (!node->pruned) return node;
    }
    return nullptr;
}

void BestFirstSelector::add_node(BnBNode* node) {
    if (!node->pruned) queue_.push(node);
}

BnBNode* DepthFirstSelector::select_node(std::vector<BnBNode*>& candidates) {
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        stack_.push_back(*it);
    }
    candidates.clear();

    while (!stack_.empty()) {
        BnBNode* node = stack_.back();
        stack_.pop_back();
        if (!node->pruned) return node;
    }
    return nullptr;
}

void DepthFirstSelector::add_node(BnBNode* node) {
    if (!node->pruned) stack_.push_back(node);
}

BnBNode* BestEstimateSelector::select_node(std::vector<BnBNode*>& candidates) {
    Compare cmp(incumbent_);
    for (auto* node : candidates) {
        queue_.push(node);
    }
    candidates.clear();

    while (!queue_.empty()) {
        BnBNode* node = queue_.top();
        queue_.pop();
        if (!node->pruned) return node;
    }
    return nullptr;
}

void BestEstimateSelector::add_node(BnBNode* node) {
    if (!node->pruned) {
        queue_.push(node);
    }
}

void BestEstimateSelector::set_incumbent(double incumbent) {
    if (incumbent == incumbent_) return;
    incumbent_ = incumbent;
    // Rebuild queue with new comparator
    std::vector<BnBNode*> nodes;
    while (!queue_.empty()) {
        nodes.push_back(queue_.top());
        queue_.pop();
    }
    Compare cmp(incumbent_);
    queue_ = decltype(queue_)(cmp);
    for (auto* n : nodes) queue_.push(n);
}

HybridSelector::HybridSelector(int dive_depth)
    : dive_depth_(dive_depth) {
    best_first_queue_ = decltype(best_first_queue_)(
        [](BnBNode* a, BnBNode* b) { return a->lower_bound > b->lower_bound; });
    best_estimate_queue_ = decltype(best_estimate_queue_)(
        [this](BnBNode* a, BnBNode* b) {
            double est_a = a->lower_bound + (incumbent_ - a->lower_bound) * 0.5;
            double est_b = b->lower_bound + (incumbent_ - b->lower_bound) * 0.5;
            return est_a > est_b;
        });
}

BnBNode* HybridSelector::select_node(std::vector<BnBNode*>& candidates) {
    for (auto* node : candidates) {
        switch (current_phase_) {
            case Phase::DIVE:
                dive_stack_.push_back(node);
                break;
            case Phase::BEST_FIRST:
                best_first_queue_.push(node);
                break;
            case Phase::BEST_ESTIMATE:
                best_estimate_queue_.push(node);
                break;
        }
    }
    candidates.clear();

    while (true) {
        if (empty()) return nullptr;
        switch (current_phase_) {
            case Phase::DIVE:
                if (!dive_stack_.empty() && dive_count_ < dive_depth_) {
                    BnBNode* node = dive_stack_.back();
                    dive_stack_.pop_back();
                    if (!node->pruned) {
                        dive_count_++;
                        return node;
                    }
                } else {
                    current_phase_ = Phase::BEST_FIRST;
                    dive_count_ = 0;
                }
                break;

            case Phase::BEST_FIRST:
                if (!best_first_queue_.empty()) {
                    BnBNode* node = best_first_queue_.top();
                    best_first_queue_.pop();
                    if (!node->pruned) return node;
                } else {
                    current_phase_ = Phase::BEST_ESTIMATE;
                }
                break;

            case Phase::BEST_ESTIMATE:
                if (!best_estimate_queue_.empty()) {
                    BnBNode* node = best_estimate_queue_.top();
                    best_estimate_queue_.pop();
                    if (!node->pruned) return node;
                } else {
                    current_phase_ = Phase::DIVE;
                }
                break;
        }
    }
    return nullptr;
}

void HybridSelector::add_node(BnBNode* node) {
    if (node->pruned) return;
    switch (current_phase_) {
        case Phase::DIVE:
            dive_stack_.push_back(node);
            break;
        case Phase::BEST_FIRST:
            best_first_queue_.push(node);
            break;
        case Phase::BEST_ESTIMATE:
            best_estimate_queue_.push(node);
            break;
    }
}

void HybridSelector::set_incumbent(double incumbent) {
    if (incumbent == incumbent_) return;
    incumbent_ = incumbent;
    // Rebuild the best_estimate_queue_ since the comparator depends on incumbent_
    std::vector<BnBNode*> nodes;
    while (!best_estimate_queue_.empty()) {
        nodes.push_back(best_estimate_queue_.top());
        best_estimate_queue_.pop();
    }
    // best_estimate_queue_ already captures `this`, so we just re-instantiate it
    best_estimate_queue_ = decltype(best_estimate_queue_)(
        [this](BnBNode* a, BnBNode* b) {
            double est_a = a->lower_bound + (incumbent_ - a->lower_bound) * 0.5;
            double est_b = b->lower_bound + (incumbent_ - b->lower_bound) * 0.5;
            return est_a > est_b;
        });
    for (auto* n : nodes) best_estimate_queue_.push(n);
}

bool HybridSelector::empty() const {
    return dive_stack_.empty() && best_first_queue_.empty() && best_estimate_queue_.empty();
}

std::size_t HybridSelector::size() const {
    return dive_stack_.size() + best_first_queue_.size() + best_estimate_queue_.size();
}

std::unique_ptr<NodeSelector> create_selector(NodeSelectionStrategy strategy, int dive_depth) {
    switch (strategy) {
        case NodeSelectionStrategy::BEST_FIRST:
            return std::make_unique<BestFirstSelector>();
        case NodeSelectionStrategy::DEPTH_FIRST:
            return std::make_unique<DepthFirstSelector>();
        case NodeSelectionStrategy::BEST_ESTIMATE:
            return std::make_unique<BestEstimateSelector>();
        case NodeSelectionStrategy::HYBRID:
            return std::make_unique<HybridSelector>(dive_depth);
    }
    return std::make_unique<HybridSelector>(dive_depth);
}

} // namespace hypernova::milp