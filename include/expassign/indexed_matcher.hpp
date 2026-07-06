#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "expassign/bitset.hpp"
#include "expassign/hash.hpp"
#include "expassign/matcher.hpp"
#include "expassign/region_tree.hpp"
#include "expassign/version.hpp"

namespace expassign {

// Оптимизированный матчер на инвертированных индексах.
//
// Идея: для каждого свойства строится индекс, который за O(1)-O(log K)
// возвращает битсет экспериментов, чьё ограничение на это свойство выполнено
// (эксперименты без ограничения на свойство в битсете всегда взведены).
// Пересечение битсетов по всем свойствам — кандидаты; для них остаётся только
// проверка слота измерения и выбор группы по хешу.
//
//   * строки — hash-map значение -> списки экспериментов (отдельно для
//     позитивных и негативных наборов);
//   * булевы — три готовых битсета (true / false / нет ограничения);
//   * версии — все границы интервалов слоя режут ось версий на элементарные
//     регионы, для каждого региона предвычислен битсет; поиск региона —
//     бинарный поиск по границам;
//   * домены — префиксное дерево по меткам хоста от TLD к младшим со
//     специальными рёбрами для '{tld}' и пометками '*';
//   * регионы — hash-map «регион -> эксперименты», проверяемая для каждого
//     предка региона запроса по цепочке дерева регионов.
//
// Если у эксперимента несколько ограничений на одно свойство (например, своё
// и унаследованное от измерения), они раскладываются по «слоям» одного и того
// же свойства; слои пересекаются так же, как разные свойства.
class IndexedMatcher final : public IMatcher {
 public:
  IndexedMatcher(std::vector<FlatExperiment> experiments,
                 std::shared_ptr<const IHasher> hasher);

  std::vector<Assignment> Match(const Request& request) const override;

  const std::vector<FlatExperiment>& Experiments() const { return experiments_; }

 private:
  struct StringLayer {
    std::string property;
    DynamicBitset unconstrained;  // эксперименты без ограничения в этом слое
    DynamicBitset negated;        // эксперименты с ограничением "не входит"
    std::unordered_map<std::string, std::vector<uint32_t>> in_lists;
    std::unordered_map<std::string, std::vector<uint32_t>> not_in_lists;

    void Filter(const Request& request, DynamicBitset& out) const;
  };

  struct BoolLayer {
    std::string property;
    DynamicBitset unconstrained;
    DynamicBitset when_true;   // unconstrained | эксперименты с expected=true
    DynamicBitset when_false;  // unconstrained | эксперименты с expected=false

    void Filter(const Request& request, DynamicBitset& out) const;
  };

  struct VersionLayer {
    std::string property;
    DynamicBitset unconstrained;
    // Отсортированные уникальные границы интервалов. Ось версий делится на
    // 2*points.size()+1 регионов: регион 2i — открытый промежуток перед
    // points[i], регион 2i+1 — сама точка points[i].
    std::vector<Version> points;
    std::vector<DynamicBitset> regions;  // unconstrained уже вмержен

    void Filter(const Request& request, DynamicBitset& out) const;
  };

  struct DomainLayer {
    struct Node {
      std::unordered_map<std::string, uint32_t> children;
      int32_t tld_child = -1;  // ребро '{tld}' (любая метка на позиции TLD)
      std::vector<uint32_t> star_exps;   // шаблоны '*.<suffix>' с концом здесь
      std::vector<uint32_t> exact_exps;  // шаблоны без '*' с концом здесь
    };

    std::string property;
    DynamicBitset unconstrained;
    std::vector<Node> nodes;  // nodes[0] — корень

    uint32_t AddChild(uint32_t parent, const std::string& label);
    uint32_t AddTldChild(uint32_t parent);
    void Filter(const Request& request, DynamicBitset& out) const;
  };

  struct RegionLayer {
    std::string property;
    // Все ограничения слоя обязаны разделять одно дерево регионов
    // (валидируется при построении).
    std::shared_ptr<const RegionTree> tree;
    DynamicBitset unconstrained;
    DynamicBitset negated;  // эксперименты с ограничением "не входит"
    std::unordered_map<uint32_t, std::vector<uint32_t>> in_lists;
    std::unordered_map<uint32_t, std::vector<uint32_t>> not_in_lists;

    void Filter(const Request& request, DynamicBitset& out) const;
  };

  // Ключ кеша хешей: (тип идентификатора, соль).
  struct HashKey {
    std::string id_key;
    std::string salt;
  };
  struct ExperimentHashRefs {
    uint32_t dim_hash = 0;  // индекс в hash_keys_ (валиден при has_dimension)
    uint32_t exp_hash = 0;
  };

  void BuildIndexes();
  uint32_t InternHashKey(const std::string& id_key, const std::string& salt);

  std::vector<FlatExperiment> experiments_;
  std::shared_ptr<const IHasher> hasher_;

  std::vector<StringLayer> string_layers_;
  std::vector<BoolLayer> bool_layers_;
  std::vector<VersionLayer> version_layers_;
  std::vector<DomainLayer> domain_layers_;
  std::vector<RegionLayer> region_layers_;

  std::vector<HashKey> hash_keys_;
  std::unordered_map<std::string, uint32_t> hash_key_index_;
  std::vector<ExperimentHashRefs> hash_refs_;
};

}  // namespace expassign
