/*-------------------------------------------------------------------------------
  This file is part of policytree.

  policytree is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  policytree is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with policytree. If not, see <http://www.gnu.org/licenses/>.
#-------------------------------------------------------------------------------*/
#include <functional>
#include <boost/container/flat_set.hpp>
#include <iostream>
#include <fstream>
#include "tree_search.h"

typedef boost::container::flat_set<Point, std::function<bool(const Point&, const Point&)>> flat_set;

/**
 * Create a vector of sorted sets
 *
 * @param data: the data class
 * @param make_empty: boolean, whether to fill the sets with points or not.
 * @return a vector (size data->num_features()) of sorted sets, each set containing
 *  data->num_points points where set at index j is sorted along dimension j
 *  (empty if `make_empty=true`, but new points will be sorted along dimension j).
 *
 *  Details:
 *                         1, ...,        j            p      1, ...,         d
 *                        +----------------------------+      +---------------+
 *  Point {} +-------->   |                            |      |               |
 *                        |                            |      |               |
 *  (sample i sorted      |                            |      |               |
 *  according to          |                            |      |               |
 *                        |                            |      |               |
 *                        |            X               |      |       Y       |
 *                        |                            |      |               |
 *                        |                            |      |               |
 *                        |                            |      |               |
 *                      N +----------------------------+      +---------------+
 *
 *                                     +
 *                                     |
 *                                     |
 *                                     v
 *
 *                        +----+----+------------------+
 * <Vector> sorted_sets   | +  |    |                  |
 *                        | |  |    |                  |
 *                        +----+----+------------------+
 *                          |
 *                          |
 *                          v
 *                     All points
 *                     sorted along
 *                     dimension 1
 *
 *
 * A boost::flat_set is used over a stl::set since the flat_set is contiguous and
 * allows for faster iteration.
 */
std::vector<flat_set> create_sorted_sets(const Data* data, bool make_empty=false) {
  std::vector<flat_set> res;
  res.reserve(data->num_features());

  for (size_t cmp_dim = 0; cmp_dim < data->num_features(); cmp_dim++) {
    auto cmp_func = [cmp_dim](const Point& lhs, const Point& rhs) {
      // if covariates have the same value use the sample index as tie-breaker
      const auto &a = lhs.get_value(cmp_dim);
      const auto &b = rhs.get_value(cmp_dim);
      if (a == b) {
        return lhs.sample < rhs.sample;
      } else {
        return a < b;
      }
    };

    flat_set setj(cmp_func);
    if (!make_empty) {
      setj.reserve(data->num_rows);
      for (size_t i = 0; i < data->num_rows; i++) {
        setj.emplace(i, data);
      }
    }
    res.push_back(setj);
  }

  return res;
}


// Find the best action in a leaf node (O(nd))
std::unique_ptr<Node> level_zero_learning(const std::vector<flat_set>& sorted_sets,
                                          const Data* data,
                                          std::vector<int>& max_treatment_size) {
  size_t num_points = sorted_sets[0].size();
  size_t num_rewards = data->num_rewards();
  size_t best_action = 0;
  double best_reward = -INF;

  std::vector<double> reward_sum(num_rewards, 0.0);

  for (size_t d = 0; d < num_rewards; d++) {
    for (const auto& point : sorted_sets[0]) {
      reward_sum[d] += point.get_reward(d);
    }
    if (reward_sum[d] > best_reward && max_treatment_size[d]-(int)num_points>=0) {
      best_reward = reward_sum[d];
      best_action = d;
    }
  }
  max_treatment_size[best_action] = max_treatment_size[best_action]-(int)num_points;
  return std::unique_ptr<Node> (new Node(0, 0.0, best_reward, best_action,max_treatment_size));
}


// Find the best action (left and right) in the parent of a leaf node (O(npd))
std::unique_ptr<Node> level_one_learning(const std::vector<flat_set>& sorted_sets,
                                         const Data* data,
                                         std::vector<std::vector<double>>& sum_array,
                                         int split_step,
                                         size_t min_node_size,
                                         std::vector<int>& max_treatment_size) {
  size_t num_points = sorted_sets[0].size();
  size_t num_rewards = data->num_rewards();
  size_t num_features = data->num_features();

  size_t best_action_left = 0;
  size_t best_action_right = 0;
  size_t best_n = 0;
  double split_val = 0.0;
  size_t split_var = 0;
  double best_reward = -INF;
  double global_best_left = -INF;
  double global_best_right = -INF;

  for (size_t p = 0; p < num_features; p++) {
    // Fill the reward matrix with cumulative sums
    for (size_t d = 0; d < num_rewards; d++) {
      size_t n = 0;
      for (const auto &point : sorted_sets[p]) {
        ++n;
        sum_array[d][n] = sum_array[d][n - 1] + point.get_reward(d);
      }
    }
    auto it = sorted_sets[p].cbegin();
    int split_counter = 0;
    size_t samples_counter = 0;
    size_t n = 0;
    for (;;) {
      ++n;
      auto value = it->get_value(p);
      ++it;
      if (it == sorted_sets[p].end()) {
        break;
      }
      auto next_value = it->get_value(p);
      split_counter += 1;
      samples_counter += 1;
      if (value == next_value) {
        continue;
      }
      if (samples_counter < min_node_size || num_points - samples_counter < min_node_size) {
        continue;
      }
      if (split_counter >= split_step) { // only split at every `split_step`th sample
        split_counter = 0;
      } else {
        continue;
      }
      double left_best = -INF;
      double right_best = -INF;
      size_t left_action = 0;
      size_t right_action = 0;
      for (size_t d = 0; d < num_rewards; d++) {
        int max_treatment_size_temp = max_treatment_size[d];
        double left_reward = sum_array[d][n];
        double right_reward = sum_array[d][num_points] - left_reward;
        if (left_best < left_reward && max_treatment_size_temp-(int)n >= 0) {
          left_best = left_reward;
          left_action = d;
          max_treatment_size_temp = max_treatment_size_temp-(int)n;
          //std::ofstream logfile("log.txt", std::ios::app);
          //logfile << "n: " << n << std::endl;
          //logfile << "max_treatment_size_temp-n: " << max_treatment_size_temp << std::endl;
          //logfile << "left_action: " << d << std::endl;
          //logfile.close();
        }
        if (right_best < right_reward && max_treatment_size_temp-((int)num_points-(int)n) >= 0) {
          right_best = right_reward;
          right_action = d;
          //std::ofstream logfile("log.txt", std::ios::app);
          //logfile << "n: " << n << std::endl;
          //logfile << "max_treatment_size_temp-n: " << max_treatment_size_temp-((int)num_points-(int)n)<< std::endl;
          //logfile << "right_action: " << d << std::endl;
          //logfile.close();
        }
      }
      if (best_reward < left_best + right_best) {
        best_reward = left_best + right_best;
        global_best_left = left_best;
        global_best_right = right_best;
        best_action_left = left_action;
        best_action_right = right_action;
        best_n = n;
        split_var = p;
        split_val = value;
      }
    }
  }
  if (best_reward > -INF) {
    // "pruning": if both actions are the same then treat this as a leaf node
    if (best_action_left == best_action_right) {
      max_treatment_size[best_action_left]=max_treatment_size[best_action_left]-(int)best_n;
      max_treatment_size[best_action_right]=max_treatment_size[best_action_right]-((int)num_points-(int)best_n);
      return std::unique_ptr<Node> (new Node(0, 0.0, best_reward, best_action_left,max_treatment_size));
    } else {
      max_treatment_size[best_action_left]=max_treatment_size[best_action_left]-(int)best_n;
      auto left = std::unique_ptr<Node> (new Node(0, 0.0, global_best_left, best_action_left,max_treatment_size));
      max_treatment_size[best_action_right]=max_treatment_size[best_action_right]-((int)num_points-(int)best_n);
      auto right = std::unique_ptr<Node> (new Node(0, 0.0, global_best_right, best_action_right,max_treatment_size));
      auto ans = std::unique_ptr<Node> (new Node(split_var, split_val, best_reward, 0,max_treatment_size));
      ans->left_child = std::move(left);
      ans->right_child = std::move(right);
      return ans;
    }
  } else {
    return level_zero_learning(sorted_sets, data,max_treatment_size);
  }
}


/**
 * Find the tree that maximizes the sum of rewards.
 *
 * Is called recursively to find to the best tree.
 *
 * The following is a depth 2 tree with optimal actions A,B,C,D.
 * Here, each hexagonal node denotes a split.
 *            ___
 *           /   \
 *           \___/
 *           +   +
 *       ___+     +___
 *      /   \     /   \
 *      \___/     \___/
 *      +   +     +   +
 *     +     +   +     +
 *     A     B   C     D
 *
 * If C is equal to D, the tree is pruned and the following returned:
 *           ___
 *          /   \
 *          \___/
 *          +   +
 *      ___+     +
 *     /   \     C
 *     \___/
 *     +   +
 *    +     +
 *    A     B
 *
 * The actions are column indices of the reward matrix in data.
 *
 * @param sorted_sets: A vector of sorted sets
 * @param level: The tree depth
 * @param split_step An optional approximation parameter, the number of possible splits to consider when
 *  performing tree search. split_step = 1 considers every possible split, split_step = 10
 *  considers splitting at every 10'th sample and may give a substantial speedup on dense features.
 * @param min_node_size An integer indicating the smallest terminal node size permitted.
 * @param data: The data class
 * @param sum_array: A global zero initialized (num_rewards) x (num_points + 1)
 *  array which is used to calculate cumulative rewards.
 * @return The best tree
 *
 * Details:
 * This algorithm maintains the data structure sorted_sets to quickly obtain
 * the sort order of points along all dimensions p for a given split.
 *
 * For each p * (N - 1) possible splits, for each dimension d:
 *   All points on the right side are stored in right_sorted_sets[d]
 *   All points on the left side are stored in left_sorted_sets[d]
 *   For each split candidate, the point is moved from the right set to the left
 *   set for all dimensions.
 *   This proceeds recursively to enumerate the reward in all
 *   possible split.
 *
 * The split condition reads: if value <= split value, go to left, else right.
 *
 * Time complexity (k >= 1): O(p^k n^k (log n + d) + pnlog n) where p is the number of
 * features, n the number of observations, d the number of actions, and k
 * the tree depth.
 */
std::unique_ptr<Node> find_best_split(const std::vector<flat_set>& sorted_sets,
                                      int level,
                                      int split_step,
                                      size_t min_node_size,
                                      const Data* data,
                                      std::vector<std::vector<double>>& sum_array,
                                      std::vector<int>& max_treatment_size) {
  if (level == 0) {
    // this base case will only be hit if `find_best_split` is called directly with level = 0
    return level_zero_learning(sorted_sets, data, max_treatment_size);
  } else if (level == 1) {
    // if at the parent of a leaf node we can compute the optimal action for both leaves
    return level_one_learning(sorted_sets, data, sum_array, split_step, min_node_size, max_treatment_size);
  // else continue the recursion
  } else {
    size_t num_points = sorted_sets[0].size();
    size_t num_features = data->num_features();
    size_t best_split_var;
    double best_split_val;
    size_t best_n = 0;
    std::unique_ptr<Node> best_left_child = nullptr;
    std::unique_ptr<Node> best_right_child = nullptr;
    std::unique_ptr<Node> best_ans_as_leaf = nullptr;

    for (size_t p = 0; p < num_features; p++) {
      auto right_sorted_sets = sorted_sets; // copy operator
      auto left_sorted_sets = create_sorted_sets(data, true); // empty
      int split_counter = 0;
      size_t samples_counter = 0;
      for (size_t n = 0; n < num_points - 1; n++) {
        std::vector<int> copied_max_treatment_size;
        copied_max_treatment_size = max_treatment_size;
        auto point = right_sorted_sets[p].cbegin(); // O(1)
        Point point_bk = *point; // store the Point instance since the iterator will be invalid after erase
        right_sorted_sets[p].erase(point); // O(1)
        left_sorted_sets[p].insert(point_bk); // O(log n)
        for (size_t j = 0; j < num_features; j++) {
          if (j == p) {
            continue;
          }
          auto to_erase = right_sorted_sets[j].find(point_bk); // O(log n)
          right_sorted_sets[j].erase(to_erase); // O(1)
          left_sorted_sets[j].insert(point_bk); // O(log n)
        }
        auto next = right_sorted_sets[p].cbegin(); // O(1)
        split_counter += 1;
        samples_counter += 1;
        if (point_bk.get_value(p) >= next->get_value(p)) { // are the values the same then skip
          continue;
        }
        if (samples_counter < min_node_size || num_points - samples_counter < min_node_size) {
          continue;
        }
        if (split_counter >= split_step) { // only split at every `split_step`th sample
          split_counter = 0;
        } else {
          continue;
        }
        auto left_child = find_best_split(left_sorted_sets, level - 1, split_step, min_node_size, data, sum_array,copied_max_treatment_size);
        copied_max_treatment_size = left_child->max_treatment_size;
        auto right_child = find_best_split(right_sorted_sets, level - 1, split_step, min_node_size, data, sum_array,copied_max_treatment_size);
        if ((best_left_child == nullptr) ||
            (left_child->reward + right_child->reward >
              best_left_child->reward + best_right_child->reward)) {
          best_left_child = std::move(left_child);
          best_right_child = std::move(right_child);
          best_n = n;
          best_split_var = p;
          best_split_val = point_bk.get_value(p);
        }
      }
    }
    if (best_left_child == nullptr) {
      return level_zero_learning(sorted_sets, data, max_treatment_size);
    } else {
      max_treatment_size[best_left_child->action_id] = max_treatment_size[best_left_child->action_id]-(int)best_n;
      max_treatment_size[best_right_child->action_id] = max_treatment_size[best_right_child->action_id]-((int)num_points-(int)best_n);
          // "pruning", the recursive case (same action in both leaves):
      if ((best_left_child->is_leaf() && best_right_child->is_leaf()) &&
              (best_left_child->action_id == best_right_child->action_id)) {
        double leaf_reward = best_left_child->reward + best_right_child->reward;
        size_t leaf_action = best_left_child->action_id;
        best_ans_as_leaf = std::unique_ptr<Node> (new Node(0, 0.0, leaf_reward, leaf_action, max_treatment_size));
        return best_ans_as_leaf;
      } else {
        double best_reward = best_left_child->reward + best_right_child->reward;
        auto ret = std::unique_ptr<Node> (new Node(best_split_var, best_split_val, best_reward, 0,max_treatment_size));
        ret->left_child = std::move(best_left_child);
        ret->right_child = std::move(best_right_child);
        return ret;
      }
    }
  }
}

std::unique_ptr<Node> tree_search(int depth, int split_step, size_t min_node_size, const Data* data, std::vector<int>& max_treatment_size) {
  size_t num_rewards = data->num_rewards();
  size_t num_points = data->num_rows;
  auto sorted_sets = create_sorted_sets(data);

  std::vector<std::vector<double>> sum_array;
  sum_array.resize(num_rewards);
  for (auto& v : sum_array) {
    // + 1 because this is a cumulative sum of rewards, entry 0 will be zero.
    v.resize(num_points + 1, 0.0);
  }

  return find_best_split(sorted_sets, depth, split_step, min_node_size, data, sum_array, max_treatment_size);
}
// split_step: equivalent to A in Zhou et al. 2023, # of points to split