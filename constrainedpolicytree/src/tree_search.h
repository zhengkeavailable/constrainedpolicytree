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
#ifndef TREE_SEARCH_H
#define TREE_SEARCH_H

#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

const double INF = std::numeric_limits<double>::infinity();

// Data class for column major storage
class Data {
public:
  Data(const double* data_x,
       const double* data_y,
       size_t num_rows,
       size_t num_cols_x,
       size_t num_cols_y) :
  num_rows(num_rows), data_x(data_x), data_y(data_y),
  num_cols_x(num_cols_x), num_cols_y(num_cols_y) {
  }

  double get_x(size_t row, size_t col) const {
    return data_x[col * num_rows + row];
  }

  double get_y(size_t row, size_t col) const {
    return data_y[col * num_rows + row];
  }

  size_t num_features() const {
    return num_cols_x;
  }

  size_t num_rewards() const {
    return num_cols_y;
  }

  size_t num_rows;

private:
  const double* data_x;
  const double* data_y;
  size_t num_cols_x;
  size_t num_cols_y;
};


// A point is a row in the [X Y] matrix
class Point {
public:
  Point (size_t sample, const Data* data) :
  sample(sample), data(data) {}

  // i is the column index of the feature matrix
  double get_value(size_t i) const {
    return data->get_x(sample, i);
  }

  // j is the column index of the reward
  double get_reward(size_t j) const {
    return data->get_y(sample, j);
  }

  size_t sample;

private:
  const Data* data;
};


struct Node {
  Node(size_t index, double value, double reward, size_t action_id,std::vector<int>& max_treatment_size) :
  index(index), value(value), reward(reward), action_id(action_id), max_treatment_size(max_treatment_size){
    this->left_child = nullptr;
    this->right_child = nullptr;
  }

  // A node is a leaf node if its left or right child node is a nullptr.
  // If one child is a nullptr, the other child is also a nullptr.
  bool is_leaf() {
    return (left_child == nullptr);
  }

  size_t index;
  double value;
  double reward;
  size_t action_id;
  std::vector<int> max_treatment_size;
  std::unique_ptr<Node> left_child;
  std::unique_ptr<Node> right_child;
};


std::unique_ptr<Node> tree_search(int, int, size_t, const Data*, std::vector<int>&);

#endif // TREE_SEARCH_H
