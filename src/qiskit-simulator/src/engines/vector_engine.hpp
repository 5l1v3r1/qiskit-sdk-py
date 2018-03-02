/*
Copyright (c) 2017 IBM Corporation. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

/**
 * @file    state_vector_engine.hpp
 * @brief   QISKIT Simulator QubitVector engine class
 * @author  Christopher J. Wood <cjwood@us.ibm.com>
 */

#ifndef _VectorEngine_h_
#define _VectorEngine_h_

#include <sstream>
#include <stdexcept>

#include "qubit_vector.hpp"
#include "base_engine.hpp"
#include "misc.hpp"

namespace QISKIT {

/***************************************************************************/ /**
 *
 * VectorEngine class
 *
 * This class is derived from the BaseEngine class. It includes all options
 * and results of the BaseEngine, and in addition may compute properties
 * related to the state vector of the backend. These include:
 * - The density matrix of the snapshoted quantum states of the system system
 *   averaged over all shots
 * - The Z-basis measurement probabilities of the snapshoted quantum states of the
 *   system averaged over all shots. Note that this is equal to the diagonal of
 *   the density matrix.
 * - The ket representation of the snapshoted quantum states of the sytem for each
 *   shot
 * - The inner product with a set of target states of the snapshoted quantum states
 *   of the system for each shot.
 * - The expectation values of a set  of target states of the snapshoted quantum 
 *   states of the system averaged over shots.
 *
 ******************************************************************************/

class VectorEngine : public BaseEngine<QubitVector> {

public:
  // Default constructor
  VectorEngine(uint_t dim = 2) : BaseEngine<QubitVector>(), qudit_dim(dim){};

  //============================================================================
  // Configuration
  //============================================================================
  uint_t qudit_dim = 2;   // dimension of each qubit as qudit
  double epsilon = 1e-10; // Chop small numbers

  bool show_snapshots_ket = false;           // show snapshots as ket-vector
  bool show_snapshots_density = false;       // show snapshots as density matrix
  bool show_snapshots_probs = false;         // show snapshots as probability vector
  bool show_snapshots_probs_ket = false;     // show snapshots as probability ket-vector
  bool show_snapshots_inner_product = false; // show inner product with snapshots
  bool show_snapshots_overlaps = false;      // show overlaps with snapshots

  std::vector<QubitVector> target_states; // vector of target states

  //============================================================================
  // Results / Data
  //============================================================================

  // Snapshots output data
  std::vector<std::map<uint_t, cket_t>> snapshots_ket;
  std::map<uint_t, cmatrix_t> snapshots_density;
  std::map<uint_t, rvector_t> snapshots_probs;
  std::map<uint_t, std::map<std::string, double>> snapshots_probs_ket;
  std::map<uint_t, std::vector<cvector_t>> snapshots_inprods;
  std::map<uint_t, rvector_t> snapshots_overlaps;

  //============================================================================
  // Methods
  //============================================================================

  // Adds results data from another engine.
  void add(const VectorEngine &eng);

  // Overloads the += operator to combine the results of different engines
  VectorEngine &operator+=(const VectorEngine &eng) {
    add(eng);
    return *this;
  };

  // Compute results
  void compute_results(Circuit &circ, BaseBackend<QubitVector> *be);

  // Convert a complex vector or ket to a real one
  double get_probs(const complex_t &val) const;
  rvector_t get_probs(const QubitVector &vec) const;
  std::map<std::string, double> get_probs(const cket_t &ket) const;
};

/***************************************************************************/ /**
  *
  * VectorEngine methods
  *
  ******************************************************************************/

void VectorEngine::add(const VectorEngine &eng) {

  BaseEngine<QubitVector>::add(eng);

  /* Accumulated snapshot sdata */

  // copy snapshots ket-maps
  std::copy(eng.snapshots_ket.begin(), eng.snapshots_ket.end(),
            back_inserter(snapshots_ket));

  // Add snapshots density
  for (const auto &s : eng.snapshots_density) {
    auto &rho = snapshots_density[s.first];
    if (rho.size() == 0)
      rho = s.second;
    else
      rho += s.second;
  }

  // Add snapshots probs
  for (const auto &s : eng.snapshots_probs) {
    snapshots_probs[s.first] += s.second;
  }

  // Add snapshots probs ket
  for (const auto &s : eng.snapshots_probs_ket)
    snapshots_probs_ket[s.first] += s.second;

  // Add snapshots overlaps
  for (const auto &s : eng.snapshots_overlaps)
    snapshots_overlaps[s.first] += s.second;

  // copy snapshots inner prods
  for (const auto &s : eng.snapshots_inprods)
    std::copy(s.second.begin(), s.second.end(),
              back_inserter(snapshots_inprods[s.first]));

}

//------------------------------------------------------------------------------

void VectorEngine::compute_results(Circuit &qasm, BaseBackend<QubitVector> *be) {
  // Run BaseEngine Counts
  BaseEngine<QubitVector>::compute_results(qasm, be);

  QubitVector &qreg = be->access_qreg();
  std::map<uint_t, QubitVector> &qreg_snapshots = be->access_snapshots();

  // String labels for ket form
  bool ket_form = (show_snapshots_ket || show_snapshots_probs_ket);
  std::vector<uint_t> regs;
  if (ket_form)
    for (auto it = qasm.qubit_sizes.crbegin(); it != qasm.qubit_sizes.crend();
         ++it)
      regs.push_back(it->second);

  /* Snapshot quantum state output data */
  if (snapshots.empty() == false) {
    // Ket form
    if (show_snapshots_ket || show_snapshots_probs_ket) {
      std::map<uint_t, cket_t> km;
      for (auto const &psi : qreg_snapshots)
        km[psi.first] = vec2ket(psi.second.vector(), qudit_dim, epsilon, regs);
      // snapshots kets
      if (show_snapshots_ket)
        snapshots_ket.push_back(km);
      // snapshots probabilities (ket form)
      if (show_snapshots_probs_ket)
        for (const auto &s : km) {
          rket_t tmp;
          for (const auto &vals : s.second)
            tmp[vals.first] = get_probs(vals.second);
          snapshots_probs_ket[s.first] += tmp;
        }
    }

    // add density matrix (needs renormalizing after all shots)
    if (show_snapshots_density) {
      for (auto const &psi : qreg_snapshots) {
        cmatrix_t &rho = snapshots_density[psi.first];
        if (rho.size() == 0)
          rho = outer_product(psi.second.vector(), psi.second.vector());
        else
          rho = rho + outer_product(psi.second.vector(), psi.second.vector());
      }
    }

    // add probs (needs renormalizing after all shots)
    if (show_snapshots_probs) {
      for (auto const &psi : qreg_snapshots) {
        auto &pr = snapshots_probs[psi.first];
        if (pr.empty())
          pr = get_probs(psi.second);
        else
          pr += get_probs(psi.second);
      }
    }
    // Inner products
    if (target_states.empty() == false &&
        (show_snapshots_inner_product || show_snapshots_overlaps)) {
      for (auto const &psi : qreg_snapshots) {
        // compute inner products
        cvector_t inprods;
        uint_t nstates = qreg.size();
        for (auto const &vec : target_states) {
          // check correct size
          if (vec.size() != psi.second.size()) {
            std::stringstream msg;
            msg << "error: target_state vector size \"" << vec.size()
                << "\" should be \"" << nstates << "\"";
            throw std::runtime_error(msg.str());
          }
          complex_t val = psi.second.inner_product(vec);
          chop(val, epsilon);
          inprods.push_back(val);
        }

        // add output inner products
        if (show_snapshots_inner_product)
          snapshots_inprods[psi.first].push_back(inprods);
        // Add output overlaps (needs renormalizing at output)
        if (show_snapshots_overlaps)
          snapshots_overlaps[psi.first] += get_probs(inprods);
      }
    }
  }
}

//------------------------------------------------------------------------------
double VectorEngine::get_probs(const complex_t &val) const {
  return std::real(std::conj(val) * val);
}

rvector_t VectorEngine::get_probs(const QubitVector &vec) const {
  rvector_t ret;
  for (const auto &elt : vec.vector())
    ret.push_back(get_probs(elt));
  return ret;
}

std::map<std::string, double> VectorEngine::get_probs(const cket_t &ket) const {
  std::map<std::string, double> ret;
  for (const auto &elt : ket)
    ret[elt.first] = get_probs(elt.second);
  return ret;
}

/***************************************************************************/ /**
  *
  * JSON conversion
  *
  ******************************************************************************/

inline void to_json(json_t &js, const VectorEngine &eng) {

  // Get results from base class
  const BaseEngine<QubitVector> &base_eng = eng;
  to_json(js, base_eng);

  // renormalization constant for average over shots
  double renorm = 1. / eng.total_shots;

  /* Additional snapshot output data */
  // Snapshot skets
  if (eng.show_snapshots_ket && eng.snapshots_ket.empty() == false) {
    js["quantum_state_ket"] = eng.snapshots_ket;
  }

  // Snapshots density
  if (eng.show_snapshots_density && eng.snapshots_density.empty() == false) {
    std::map<uint_t, cmatrix_t> rhos;
    for (const auto &s : eng.snapshots_density) {
      auto rho = s.second * renorm;
      chop(rho, eng.epsilon);
      rhos[s.first] = rho;
    }
    js["density_matrix"] = rhos;
  }
  // Snapshots probs
  if (eng.show_snapshots_probs && eng.snapshots_probs.empty() == false) {
    std::map<uint_t, rvector_t> ret;
    for (const auto &s : eng.snapshots_probs) {
      const auto &val = s.second;
      ret[s.first] = val * renorm;
      chop(ret[s.first], eng.epsilon);
    }
    js["probabilities"] = ret;
  }
  // Snapshots probs ket
  if (eng.show_snapshots_probs_ket && eng.snapshots_probs_ket.empty() == false) {
    std::map<uint_t, rket_t> ret;
    for (const auto &s : eng.snapshots_probs_ket) {
      const auto &val = s.second;
      ret[s.first] = val * renorm;
      chop(ret[s.first], eng.epsilon);
    }
    js["probabilities_ket"] = ret;
  }

  // Snapshots inner products
  if (eng.show_snapshots_inner_product && eng.snapshots_inprods.empty() == false) {
    auto tmp = eng.snapshots_inprods;
    for (auto &s : tmp)
      tmp[s.first] = chop(s.second, eng.epsilon);
    js["inner_products"] = eng.snapshots_inprods;
  }
  // Snapshots overlaps
  if (eng.show_snapshots_overlaps && eng.snapshots_overlaps.empty() == false) {
    auto tmp = eng.snapshots_overlaps;
    for (auto &s : tmp) {
      s.second *= renorm;
      tmp[s.first] = chop(s.second, eng.epsilon);
    }
    js["overlaps"] = tmp;
  }
}

inline void from_json(const json_t &js, VectorEngine &eng) {
  eng = VectorEngine();
  BaseEngine<QubitVector> &base_eng = eng;
  from_json(js, base_eng);
  // Get output options
  std::vector<std::string> opts;
  if (JSON::get_value(opts, "data", js)) {
    for (auto &o : opts) {
      to_lowercase(o);
      string_trim(o);

      if (o == "quantumstateket" || o == "quantumstatesket")
        eng.show_snapshots_ket = true;
      else if (o == "densitymatrix")
        eng.show_snapshots_density = true;
      else if (o == "probabilities" || o == "probs")
        eng.show_snapshots_probs = true;
      else if (o == "probabilitiesket" || o == "probsket")
        eng.show_snapshots_probs_ket = true;
      else if (o == "targetstatesinner")
        eng.show_snapshots_inner_product = true;
      else if (o == "targetstatesprobs")
        eng.show_snapshots_overlaps = true;
    }
  }
  // Get additional settings
  JSON::get_value(eng.epsilon, "chop", js);
  JSON::get_value(eng.qudit_dim, "qudit_dim", js);

  // renormalize state vector
  if (eng.initial_state_flag)
    eng.initial_state.renormalize();

  // parse target states from JSON
  bool renorm_target_states = true;
  JSON::get_value(renorm_target_states, "renorm_target_states", js);
  if (JSON::get_value(eng.target_states, "target_states", js) &&
      renorm_target_states)
    for (auto &qv : eng.target_states)
      qv.renormalize();
}

//------------------------------------------------------------------------------
} // end namespace QISKIT

#endif