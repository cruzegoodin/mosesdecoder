/*
 * PhraseTableMemory.cpp
 *
 *  Created on: 28 Oct 2015
 *      Author: hieu
 */

#include <cassert>
#include <boost/foreach.hpp>
#include "PhraseTableMemory.h"
#include "../PhraseImpl.h"
#include "../Phrase.h"
#include "../System.h"
#include "../Scores.h"
#include "../InputPathsBase.h"
#include "../legacy/InputFileStream.h"

#include "../PhraseBased/InputPath.h"
#include "../PhraseBased/TargetPhraseImpl.h"
#include "../PhraseBased/TargetPhrases.h"

#include "../SCFG/PhraseImpl.h"
#include "../SCFG/TargetPhraseImpl.h"
#include "../SCFG/InputPath.h"

using namespace std;

namespace Moses2
{

PhraseTableMemory::Node::Node() :
    m_targetPhrases(NULL), m_unsortedTPS(NULL)
{
}

PhraseTableMemory::Node::~Node()
{
}

void PhraseTableMemory::Node::AddRule(Phrase &source, TargetPhrase *target)
{
  AddRule(source, target, 0);
}

PhraseTableMemory::Node &PhraseTableMemory::Node::AddRule(Phrase &source,
    TargetPhrase *target, size_t pos)
{
  if (pos == source.GetSize()) {
    if (m_unsortedTPS == NULL) {
      m_unsortedTPS = new std::vector<TargetPhrase*>();
      m_source = &source;
    }

    m_unsortedTPS->push_back(target);
    return *this;
  }
  else {
    const Word &word = source[pos];
    Node &child = m_children[word];
    return child.AddRule(source, target, pos + 1);
  }
}

TargetPhrases *PhraseTableMemory::Node::Find(const Phrase &source,
    size_t pos) const
{
  assert(source.GetSize());
  if (pos == source.GetSize()) {
    return m_targetPhrases;
  }
  else {
    const Word &word = source[pos];
    //cerr << "word=" << word << endl;
    Children::const_iterator iter = m_children.find(word);
    if (iter == m_children.end()) {
      return NULL;
    }
    else {
      const Node &child = iter->second;
      return child.Find(source, pos + 1);
    }
  }
}

const PhraseTableMemory::Node *PhraseTableMemory::Node::Find(const Word &word) const
{
  Children::const_iterator iter = m_children.find(word);
  if (iter == m_children.end()) {
    return NULL;
  }
  else {
    const Node &child = iter->second;
    return &child;
  }

}

void PhraseTableMemory::Node::SortAndPrune(size_t tableLimit, MemPool &pool,
    System &system)
{
  BOOST_FOREACH(Children::value_type &val, m_children){
  Node &child = val.second;
  child.SortAndPrune(tableLimit, pool, system);
}

// prune target phrases in this node
if (m_unsortedTPS) {
  m_targetPhrases = new (pool.Allocate<TargetPhrases>()) TargetPhrases(pool, m_unsortedTPS->size());

  for (size_t i = 0; i < m_unsortedTPS->size(); ++i) {
    TargetPhrase *tp = (*m_unsortedTPS)[i];
    m_targetPhrases->AddTargetPhrase(*tp);
  }

  m_targetPhrases->SortAndPrune(tableLimit);
  system.featureFunctions.EvaluateAfterTablePruning(system.GetSystemPool(), *m_targetPhrases, *m_source);

  delete m_unsortedTPS;
}
}

////////////////////////////////////////////////////////////////////////

PhraseTableMemory::PhraseTableMemory(size_t startInd, const std::string &line) :
    PhraseTable(startInd, line)
{
  ReadParameters();
}

PhraseTableMemory::~PhraseTableMemory()
{
  // TODO Auto-generated destructor stub
}

void PhraseTableMemory::Load(System &system)
{
  FactorCollection &vocab = system.GetVocab();

  MemPool &systemPool = system.GetSystemPool();
  MemPool tmpSourcePool;
  vector<string> toks;
  size_t lineNum = 0;
  InputFileStream strme(m_path);
  string line;
  while (getline(strme, line)) {
    if (++lineNum % 1000000 == 0) {
      cerr << lineNum << " ";
    }
    toks.clear();
    TokenizeMultiCharSeparator(toks, line, "|||");
    assert(toks.size() >= 3);
    //cerr << "line=" << line << endl;

    Phrase *source;
    TargetPhrase *target;

    switch (system.options.search.algo) {
    case Normal:
    case CubePruning:
    case CubePruningPerMiniStack:
    case CubePruningPerBitmap:
    case CubePruningCardinalStack:
    case CubePruningBitmapStack:
    case CubePruningMiniStack:
      source = PhraseImpl::CreateFromString(tmpSourcePool, vocab, system,
          toks[0]);
      //cerr << "created soure" << endl;
      target = TargetPhraseImpl::CreateFromString(systemPool, *this, system,
          toks[1]);
      //cerr << "created target" << endl;
      break;
    case CYKPlus:
      source = SCFG::PhraseImpl::CreateFromString(tmpSourcePool, vocab, system,
          toks[0]);
      //cerr << "created soure" << endl;
      target = SCFG::TargetPhraseImpl::CreateFromString(systemPool, *this,
          system, toks[1]);
      //cerr << "created target" << endl;
      break;
    default:
      abort();
    }

    target->GetScores().CreateFromString(toks[2], *this, system, true);
    //cerr << "created scores:" << *target << endl;

    // properties
    if (toks.size() == 7) {
      //target->properties = (char*) system.systemPool.Allocate(toks[6].size() + 1);
      //strcpy(target->properties, toks[6].c_str());
    }

    system.featureFunctions.EvaluateInIsolation(systemPool, system, *source,
        *target);
    //cerr << "EvaluateInIsolation:" << *target << endl;
    m_root.AddRule(*source, target);
  }

  m_root.SortAndPrune(m_tableLimit, systemPool, system);
}

TargetPhrases* PhraseTableMemory::Lookup(const Manager &mgr, MemPool &pool,
    InputPathBase &inputPath) const
{
  const SubPhrase &phrase = inputPath.subPhrase;
  TargetPhrases *tps = m_root.Find(phrase);
  return tps;
}

void PhraseTableMemory::InitActiveChart(SCFG::InputPath &path) const
{
  size_t ptInd = GetPtInd();
  SCFG::ActiveChart &chart = path.GetActiveChart(ptInd);
  ActiveChartEntryMem *chartEntry = new ActiveChartEntryMem(&m_root);

  chart.entries.push_back(chartEntry);
}

void PhraseTableMemory::Lookup(MemPool &pool, const System &system, SCFG::InputPath &path) const
{
  size_t ptInd = GetPtInd();

  // terminal
  const Word &lastWord = path.subPhrase.Back();
  //cerr << "PhraseTableMemory lastWord=" << lastWord << endl;

  const SCFG::InputPath *prefixPath = static_cast<const SCFG::InputPath*>(path.prefixPath);
  assert(prefixPath);

  BOOST_FOREACH(const SCFG::ActiveChartEntry *entry, prefixPath->GetActiveChart(ptInd).entries) {
    const ActiveChartEntryMem *entryCast = static_cast<const ActiveChartEntryMem*>(entry);
    const Node *node = entryCast->node;
    const Node *nextNode = node->Find(lastWord);

    // new entries
    SCFG::ActiveChart &chart = path.GetActiveChart(ptInd);
    ActiveChartEntryMem *chartEntry = new ActiveChartEntryMem(nextNode);

    chart.entries.push_back(chartEntry);

    // look up lhs and find tps

  }

}

}

