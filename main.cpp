#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

constexpr size_t kNumLetters = 5;
const char *kValidEntryFile = "wordle-allowed-guesses.txt";
const char *kAnswerFile = "wordle-answers-alphabetical.txt";
constexpr size_t kNumGuesses = 6;

using Letter = int32_t;
using Word = std::array<Letter, kNumLetters>;
using WordList = std::vector<Word>;
using LetterField = uint32_t;

constexpr Letter kUnknownLetter = -1;

std::ostream &operator<<(std::ostream &os, const Word &word) {
  std::string str(kNumLetters, '\0');
  for (size_t i = 0; i < kNumLetters; ++i) {
    str[i] = word[i] + 'a';
  }
  os << str;
  return os;
}

WordList loadWordFile(const char *file_name) {
  std::ifstream infile(file_name);
  constexpr size_t kBufferSize = kNumLetters + 1;
  WordList word_list;
  for (std::array<char, kBufferSize> buffer;
       infile.getline(&buffer[0], kBufferSize);) {
    word_list.emplace_back();
    for (size_t i = 0; i < kNumLetters; ++i) {
      word_list.back()[i] = buffer[i] - 'a';
    }
  }
  return word_list;
}

class Clues {
public:
  Clues() {
    match_.fill(kUnknownLetter);
    wrong_.fill(0);
  }

  bool matches(const Word &word) const {

    LetterField in_word = 0;
    for (size_t i = 0; i < kNumLetters; ++i) {
      Letter letter = word[i];
      Letter correct_letter = match_[i];
      if (correct_letter != kUnknownLetter && correct_letter != letter) {
        return false;
      }

      LetterField letter_bit = 1 << letter;
      if (letter_bit & wrong_[i]) {
        return false;
      }

      in_word |= letter_bit;
    }

    if ((in_answer_ & in_word) != in_answer_) {
      return false;
    }

    return true;
  }

protected:
  Word match_;
  std::array<LetterField, kNumLetters> wrong_;
  LetterField in_answer_ = 0;
};

class WorldleGame : public Clues {
public:
  WorldleGame(const Word &answer) : Clues(), answer_(answer) {
    for (int l : answer_) {
      answer_bits_ |= 1 << l;
    }
  }

  void addGuess(const Word &guess) {
    for (size_t i = 0; i < kNumLetters; ++i) {
      Letter answer_letter = answer_[i];
      Letter guess_letter = guess[i];
      LetterField guess_letter_bit = 1 << guess_letter;
      LetterField guess_in_answer = guess_letter_bit & answer_bits_;

      in_answer_ |= guess_in_answer;

      if (guess_letter == answer_letter) {
        // Letter matches.
        match_[i] = answer_letter;
      } else if (guess_in_answer) {
        // Letter is wrong at this position.
        wrong_[i] |= guess_letter_bit;
      } else {
        // Letter appears nowhere in answer.
        for (LetterField &letter_field : wrong_) {
          letter_field |= guess_letter_bit;
        }
      }
    }
  }

  void addGuesses(std::vector<Word> &guesses) {
    for (const Word &guess : guesses) {
      addGuess(guess);
    }
  }

  WorldleGame getCopyWithGuess(const Word& guess) const {
    WordleGame ret = *this;
    ret.addGuess(guess);
    return ret;
  }

private:
  Word answer_;
  LetterField answer_bits_ = 0;
};

struct GuessScore {
  size_t idx = 0;
  int64_t score = 0; // lower is better.
};

// Print the top 50 first guesses, ranked by average amount by which they
// reduce the number of answers possible. Takes ~1 hr to complete on my
// Dell XPS running in VirtualBox.
void findGreedyFirstGuess(const WordList& entries, size_t num_answers) {
  std::vector<GuessScore> guess_scores;
  for (size_t i = 0; i < num_guesses; ++i) {
    guess_scores.push_back(GuessScore{i, 0});
  }
  auto answers_begin_it = entries.cbegin();
  auto answers_end_it = entries.cbegin() + num_answers();
  for (size_t i = 0; i < num_answers; ++i) {
    std::cout << i << " / " << num_answers << std::endl;
    for (size_t j = 0; j < entries.size(); ++j) {
      WorldleGame wordle_game(answers[i]);
      wordle_game.addGuess(entries[j]);
      guess_scores[j].score +=
          std::count_if(answers_begin_it, answers_end_it,
                        [&wordle_game](const Word &answer) { return wordle_game.matches(answer); });
    }
  }
  std::sort(guess_scores.begin(), guess_scores.end(),
            [](const GuessScore &a, const GuessScore &b) {
              return a.score < b.score;
            });
  for (size_t i = 0; i < 50; ++i) {
    std::cout << i << " guess: " << entries[guess_scores[i].idx] << " "
              << guess_scores[i].score << std::endl;
  }
}

// Returns the number of iterations to discover answer by greedy algorithm. The
// guess that reduces the search space of the next iteration is always chosen.
int runGreedyTrial(const WordList &entries, size_t answer_idx,
                   size_t num_answers, bool debug = false) {
  assert(answer_idx < entries.size());
  const Word &answer = entries[answer_idx];
  WorldleGame trial_game(answer);
  constexpr Word first_guess = {17, 14, 0, 19, 4}; // 'roate'
  trial_game.addGuess(first_guess);
  auto answers_end_it = entries.cbegin() + num_answers;
  WordList valid_answers;
  for (auto it = entries.cbegin(); it != answers_end_it; ++it) {
    if (trial_game.matches(*it)) {
      valid_answers.push_back(*it);
    }
  }

  std::vector<Word> best_guesses = {first_guess};
  std::vector<int64_t> scores;
  WordList next_valid_answers;
  int trial_count = 1;
  while (valid_answers.size() > 1) {
    for (const Word &valid_answer : valid_answers) {
      WorldleGame game_for_answer(valid_answer);
      game_for_answer.addGuesses(best_guesses);
      for (size_t i = 0; i < entries.size(); ++i) {
        WorldleGame game_with_guess = game_for_answer.getCopyWithGuess(entries[i]);
        scores[i] += std::count_if(
            valid_answers.cbegin(), valid_answers.cend(),
            [&game_with_guess](const Word &w) { return game_with_guess.matches(w); });
      }
    }
    auto min_it =
        std::min_element(scores.cbegin(), scores.cend());
    const Word &best_guess = entries[std::distance(scores.cbegin(), min_it)];
    if (debug) {
      std::cout << "best_guess: " << best_guess << std::endl;
    }
    best_guesses.push_back(best_guess);
    trial_game.addGuess(best_guess);
    for (const Word &a : valid_answers) {
      if (trial_game.matches(a)) {
        next_valid_answers.push_back(a);
      }
    }
    std::swap(valid_answers, next_valid_answers);
    std::fill(scores.begin(), scores.end(), 0);
    next_valid_answers.clear();
    ++trial_count;

    if (debug) {
      std::cout << "new valid answers: " << std::endl;
      for (const auto& w : valid_answers) {
        std::cout << w << std::endl;
      }
    }
  }
  if (debug) {
    std::cout << "Guesses:" << std::endl;
    for (const Word& w : best_guesses) {
      std::cout << "  " << w << std::endl;
    }
  }
  assert(valid_answers.size() == 1);
  assert(valid_answers.front() == answer);
  return trial_count;
}

Word fromString(const std::string &str) {
  assert(str.size() == kNumLetters);
  Word word;
  for (size_t i = 0; i < kNumLetters; ++i) {
    word[i] = str[i] - 'a';
  }
  return word;
}

/**
 * TODO: Implement a solver based on minimizing expected number of guesses:
 *  = 1 * P(g1 = ans) + 2 * P(g2 = ans | g1) + 3 * P(g3 = ans | g1, g2) + ...
 */

void run() {
  WordList non_answer_entries = loadWordFile(kValidEntryFile);
  WordList entries = loadWordFile(kAnswerFile);
  size_t num_answers = entries.size();
  entries.insert(entries.cend(), non_answer_entries.cbegin(),
                 non_answer_entries.cend());

  // Run trial on a given answer.
  std::cout << runGreedyTrial(entries, 445, num_answers, true) << std::endl;

  // Run several trials and average num guesses guess 
  int total = 0;
  int num_trials = 50;
  for (int i = 0; i < num_trials; ++i) {
    total +=
        runGreedyTrial(entries, i * (num_answers / num_trials), num_answers);
  }
  std::cout << "greedy avg: " << static_cast<float>(total) / num_trials;
}

int main() { run(); }
