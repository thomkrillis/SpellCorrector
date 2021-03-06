#include "spellCorrector.h"
#include <stdio.h>
#include <string>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <locale>
#include <math.h> //log()
#include <sstream>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fstream>
#include <time.h>

namespace SpellCorrector
{

int fillBigramList( std::list<dictEntry> ** bigramList, std::string first,
    sqlite3 *db)
{
    clock_t t;
    t = clock();
    
    std::string statement;
    int rc;
    sqlite3_stmt * ppStmt;
    
    statement = "SELECT * FROM Bigrams\nWHERE First='" + first + "'";
    rc = sqlite3_prepare_v2( db, statement.c_str(), statement.length() + 1, 
        &ppStmt, NULL);
    
    if(sqlite3_step(ppStmt) != SQLITE_ROW)
    {
        (*bigramList) = NULL;
        return 0;
    }
    
    (*bigramList) = new std::list<dictEntry>();
    
    int count = 0;
    do
    {
        int score = sqlite3_column_int(ppStmt, 2);
        count += score;
        const unsigned char * sec = sqlite3_column_text(ppStmt, 1);
        std::string second = std::string((char *)sec);
        dictEntry de = dictEntry(second, score);
        (*bigramList)->push_back(de);
    } while (rc = (sqlite3_step(ppStmt) == SQLITE_ROW));
    
    if ((rc != SQLITE_DONE)&&(rc != SQLITE_OK))
    {
        std::cout<<"Error reading bigram list "<<rc<<std::endl;
    }
    
    (*bigramList)->sort();
    (*bigramList)->reverse();
    
    t = clock() - t;
    
    //std::cout<<"time to load bigram list for "<<first<<": "<<
        //((float)t)/CLOCKS_PER_SEC<<std::endl;
    
    return count;
}

void fillBigramSet( std::unordered_map<std::string, int> ** bigramSet,
    std::string first, sqlite3 * db)
{
    clock_t t;
    t = clock();
    std::string statement;
    int rc;
    sqlite3_stmt * ppStmt;
    
    statement = "SELECT * FROM Bigrams\nWHERE First='" + first + "'";
    rc = sqlite3_prepare_v2( db, statement.c_str(), statement.length() + 1, 
        &ppStmt, NULL);
    
    if(sqlite3_step(ppStmt) != SQLITE_ROW)
    {
        (*bigramSet) = NULL;
        return;
    }
    
    (*bigramSet) = new std::unordered_map<std::string, int>();
    
    do
    {
        int score = sqlite3_column_int(ppStmt, 2);
        const unsigned char * sec = sqlite3_column_text(ppStmt, 1);
        std::string second = std::string((char *)sec);
        (*bigramSet)->insert({second, score});
    } while (sqlite3_step(ppStmt) == SQLITE_ROW);
    
    t = clock() - t;
    
    std::cout<<"time to load bigram set for "<<first<<": "<<
        ((float)t)/CLOCKS_PER_SEC<<std::endl;
    
    /*std::locale loc;
    std::string lf = std::tolower(first, loc);
    if (lf == first)
        return;
    statement = "SELECT * FROM Bigrams\nWHERE First='" + lf + "'";
    rc = sqlite3_prepare_v2( db, statement.c_str(), statement.length() + 1, 
        &ppStmt, NULL);
    while (sqlite3_step(ppStmt) == SQLITE_ROW)
    {
        int score = sqlite3_column_int(ppStmt, 2);
        const unsigned char * sec = sqlite3_column_text(ppStmt, 1);
        std::string second = std::string((char *)sec);
        (*bigramSet)->insert({second, score});
    }*/
}

std::string bestBigram(std::string first, std::string second, corrector * corr, 
    sqlite3 * db)
{
    std::list<dictEntry> * bigramList;
    int count = fillBigramList( &bigramList, first, db );
    if ((bigramList == NULL) || (count == 0))
    {
        return second;
    }
    
    std::list<entry> wordList = std::list<entry>();
    
    double maxErr = -20;
    int len;
    if ((len = second.length()) > (2*AVERAGE_WORD_LEN))
    {
        maxErr = maxErr * len / (2*AVERAGE_WORD_LEN);
    }
    
    corr->fillPossibleWordListLin(&wordList, bigramList, log(count), 
        second, maxErr);
    
    if (wordList.empty())
        return second;
    entry e = wordList.front();
    
    delete bigramList;
    return e.str;
}

double scoreBigram( std::string first, std::string second, corrector * corr,
    std::unordered_map<std::string, std::unordered_map<std::string, int> *> 
    * bigramSets, sqlite3 * db )
{
    std::string word;
    std::unordered_map<std::string, int> * bigramSet;
    double score = 0;
    if (!bigramSets->count(first))
    {
        fillBigramSet( &bigramSet, first, db);
        bigramSets->insert({first, bigramSet});
    }
    if (!bigramSets->count(first))
    {
        return 0;
    }
    bigramSet = bigramSets->at(first);
    if (bigramSet != NULL)
    {
        if (bigramSet->count(second))
        {
            score += bigramSet->at(second);
        }
    }
    return score;
}

void Viterbi2_update_vectors(std::vector<std::vector<std::string>> * words,
    std::vector<std::vector<double>>* best, std::vector<std::vector<int>>* lens,
    double p, std::string word, int w, int i, int storage_num)
{
    //std::cout<<"update "<<word<<" "<<p<<std::endl;
    int k;
    for (k = storage_num - 1; k > 0; k--)
    {
        if (p < (*best)[i][k - 1])
        {
            break;
        }
        (*best)[i][k] = (*best)[i][k - 1];
        (*words)[i][k] = (*words)[i][k - 1];
        (*lens)[i][k] = (*lens)[i][k - 1];
    }
    
    (*best)[i][k] = p;
    (*words)[i][k] = word;
    (*lens)[i][k] = w;
}

void Viterbi2_unwrap_string(std::vector<std::vector<std::string>> * words,
    std::vector<std::vector<double>>* best, std::vector<std::vector<int>>* lens,
    std::list<std::string> * output, int storage_num, 
    std::vector<int> * indices)
{
    int i = indices->size() - 1;
    int j = words->size() - 1;
    for (; (j > 0)&&(i >= 0); i--)
    {
        output->push_front((*words)[j][indices->at(i)]);
        j -= (*lens)[j][indices->at(i)];
    }
    while (j > 0)
    {
        output->push_front((*words)[j][0]);
        j -= (*lens)[j][0];
    }
}

std::string Viterbi2_create_sequence(std::list<std::string> * words)
{
    std::list<std::string>::iterator it = words->begin();
    std::string sequence = (*it);
    ++it;
    for (; it != words->end(); ++it)
    {
        sequence += " " + (*it);
    }
    return sequence;
}

std::string Viterbi2( std::string text, corrector * corr, sqlite3 * db, 
    double made_up_word_penalty, int acceptable_freq, int storage_num)
{
    clock_t t;
    t = clock();
    int n = text.length();
    std::vector<std::vector<std::string>> words = 
        std::vector<std::vector<std::string>>(n + 1, 
        std::vector<std::string>(storage_num, ""));
    words[0][0] = text;
    std::vector<std::vector<double>> best = std::vector<std::vector<double>> 
        (n + 1, std::vector<double>(storage_num, -3000.0));
    std::vector<std::vector<int>> lens = 
        std::vector<std::vector<int>>(n + 1, std::vector<int>(storage_num, n));
    best[0][0] = 0.0;
    //std::cout<<"a "<<std::endl;
    for (int i = 0; i < n + 1; i++)
    {
        for (int j = std::max(0, i - 10); j < i; j++)
        {
            //std::cout<<"1 i "<<i<<" j "<<j<<std::endl;
            bool inserted = false;
            std::string word = text.substr(j, i-j);
            int w = word.length();
            double p = -1*made_up_word_penalty;
            if (isupper(word[0]))
            {
                p = p/2;
            }
            if (w > AVERAGE_WORD_LEN)
            {
                p = p*w/AVERAGE_WORD_LEN;
            }
            //std::cout<<"b "<<std::endl;
            int count;
            //std::cout<<"c "<<std::endl;
            if (count = corr->getWordFreq(word))
            {
                p = log(count) - log(corr->getNWords());
                p += best[i - w][0];
                if (p > best[i][storage_num - 1])
                {
                    Viterbi2_update_vectors(&words, &best, &lens, 
                        p, word, w, i, storage_num);
                }
                inserted = true;
            }
            //std::cout<<"d "<<std::endl;
            std::list<entry> * wordList = new std::list<entry>();
            corr->fillPossibleWordListLin(wordList, word, -20, 1000, 
                acceptable_freq, 1);
            //std::cout<<"e "<<std::endl;
            if ((wordList != NULL)&&(!wordList->empty()))
            {
                std::list<entry>::iterator it = wordList->begin();
                while (((p = (it->d + best[i - w][0])) > best[i][storage_num - 1])
                    && (it != wordList->end()))
                {
                    if (word == it->str)
                    {
                        ++it;
                        continue;
                    }
                    Viterbi2_update_vectors(&words, &best, &lens, 
                        p, it->str, w, i, storage_num);
                }
                ++it;
                inserted = true;
            }
            //std::cout<<"f "<<std::endl;
            delete wordList;
            if (!inserted && (p > best[i][storage_num - 1]))
            {
                p += best[i - w][0];
                Viterbi2_update_vectors(&words, &best, &lens, 
                    p, word, w, i, storage_num);
            }
        }
    }
    //std::cout<<"a2 "<<std::endl;
    std::list<std::string> results = std::list<std::string>();
    
    std::vector<int> indices = std::vector<int>(n, 0);
    
    Viterbi2_unwrap_string(&words, &best, &lens, &results, 
        storage_num, &indices);
    
    std::unordered_map<std::string, std::unordered_map<std::string, int> *> 
        bigramSets = std::unordered_map<std::string, 
        std::unordered_map<std::string, int> *>();
    
    std::list<std::vector<int>> indices_list = std::list<std::vector<int>>();
    std::unordered_set<int> checked = std::unordered_set<int>();
    
    indices_list.push_front(std::vector<int>(results.size(), 0));
    //std::cout<<"b "<<std::endl;
    while (!indices_list.empty())
    {
        //std::cout<<"c "<<std::endl;
        indices = indices_list.front();
        indices_list.pop_front();
        /*int key = 0;
        for (int i = 0; i < indices.size(); i++)
        {
            key = key*storage_num + indices[i];
        }
        if (checked.count(key))
            continue;*/
        for (int i = 0; i < indices.size(); i++)
        {
            std::cout<<indices[i]<<" ";
        }
        std::cout<<std::endl;
        
        results = std::list<std::string>();
        Viterbi2_unwrap_string(&words, &best, &lens, &results, 
            storage_num, &indices);
        
        for (auto it = results.begin(); it != results.end(); ++it)
        {
            std::cout<<(*it)<<" ";
        }
        std::cout<<std::endl;
        
        if (results.empty())
            continue;
        //std::cout<<"d "<<std::endl;
        std::list<std::string>::iterator it = --(--results.end());
        std::list<std::string>::iterator it2 = --results.end();
        bool bigrams_found = (results.size() > 1);
        std::vector<int> new_indices = std::vector<int>(results.size(), 0);
        int offset = indices.size() - new_indices.size();
        for (int i = indices.size() - 1; (it2 != results.begin()) && (i >= 0);
            --it, --it2, --i)
        {
            //std::cout<<"1 i "<<i<<std::endl;
            double score = scoreBigram( (*it), (*it2), corr, &bigramSets, db );
            //std::cout<<"2 "<<std::endl;
            bigrams_found = bigrams_found && (score > 0);
            //std::cout<<"3 "<<std::endl;
            new_indices[i - offset] = indices[i];
            if (!score)
            {
                if (new_indices[i - offset] < storage_num - 1)
                {
                    new_indices[i - offset]++;
                    int key = 0;
                    for (int j = 0; j < new_indices.size(); j++)
                    {
                        key = key*storage_num + indices[j];
                    }
                    if (!checked.count(key))
                    {
                        indices_list.push_back(new_indices);
                        checked.insert(key);
                    }
                    new_indices[i - offset]--;
                }
                if (((i - offset - 1) > 0) && 
                    (new_indices[i - offset - 1] < storage_num - 1))
                {
                    new_indices[i - offset - 1]++;
                    int key = 0;
                    for (int j = 0; j < new_indices.size(); j++)
                    {
                        key = key*storage_num + indices[j];
                    }
                    if (!checked.count(key))
                    {
                        indices_list.push_back(new_indices);
                        checked.insert(key);
                    }
                    new_indices[i - offset - 1]++;
                }
            }
            //std::cout<<"4 "<<std::endl;
        }
        int ind = -1*offset;
        while (it2 != results.begin())
        {
            double score = scoreBigram( (*it), (*it2), corr, &bigramSets, db );
            bigrams_found = bigrams_found && (score > 0);
            if (!score)
            {
                if (new_indices[ind] < storage_num - 1)
                {
                    new_indices[ind]++;
                    int key = 0;
                    for (int j = 0; j < new_indices.size(); j++)
                    {
                        key = key*storage_num + indices[j];
                    }
                    if (!checked.count(key))
                    {
                        indices_list.push_back(new_indices);
                        checked.insert(key);
                    }
                    new_indices[ind]--;
                }
                if (((ind - 1) > 0) && 
                    (new_indices[ind - 1] < storage_num - 1))
                {
                    new_indices[ind - 1]++;
                    int key = 0;
                    for (int j = 0; j < new_indices.size(); j++)
                    {
                        key = key*storage_num + indices[j];
                    }
                    if (!checked.count(key))
                    {
                        indices_list.push_back(new_indices);
                        checked.insert(key);
                    }
                    new_indices[ind - 1]++;
                }
            }
            --it, --it2, --ind;
        }
        if (bigrams_found)
        {
            std::cout<<"bigrams found"<<std::endl;
            t = clock() - t;
    
            std::cout<<"time for Viterbi2: "<<
                ((float)t)/CLOCKS_PER_SEC<<std::endl;
                
            return Viterbi2_create_sequence(&results);
        }
    }
    
    results = std::list<std::string>();
    
    indices = std::vector<int>(n, 0);
    
    Viterbi2_unwrap_string(&words, &best, &lens, &results, 
        storage_num, &indices);
    
    t = clock() - t;

    std::cout<<"time for Viterbi2: "<<((float)t)/CLOCKS_PER_SEC<<std::endl;
    
    return Viterbi2_create_sequence(&results);
}

std::string correct(std::string input, double confidence, corrector * corr,
    std::string first, sqlite3 * db)
{
    std::cout<<"a "<<confidence<<std::endl;
    //int correctCase;
    std::locale loc;
    std::cout<<"b "<<corr->getWordFreq(input)<<std::endl;
    if (corr->getWordFreq(input))
    {
        std::cout<<"word found 1"<<std::endl;
        if (confidence < 90) //TODO: find good threshold
        {
            if (first == cmd_begin)
            {
                std::list<entry> wordList = std::list<entry>();
                corr->fillPossibleWordListLin(&wordList, input, -20);
                if (!wordList.empty())
                {
                    entry e = wordList.front();
                    input = e.str;
                }
            }
            else
            {
                input = bestBigram(first, input, corr, db);
            }
        }
        std::cout<<"word found 2"<<std::endl;
        return input;
    }
    std::cout<<"c"<<std::endl;
    std::list<entry> wordList = std::list<entry>();
    corr->fillPossibleWordListLin(&wordList, input, -20);
    std::cout<<"d"<<std::endl;
    if (!wordList.empty())
    {
        entry e = wordList.front();
        if (e.d > -50) //TODO: find good threshold
        {
            std::cout<<"score "<<e.d<<std::endl;
            return e.str;
        }
    }
    std::cout<<"e"<<std::endl;
    double made_up_word_penalty;
    if (isupper(input[0]))
    {
        made_up_word_penalty = (100 - confidence)*3;
    }
    else
    {
        made_up_word_penalty = (100 - confidence)*3;
    }
    std::cout<<"made up word penalty "<<made_up_word_penalty<<std::endl;
    int acceptable_freq = 1;
    std::cout<<"f"<<std::endl;
    input = Viterbi2(input, corr, db, made_up_word_penalty, acceptable_freq, 4);
    std::cout<<"g"<<std::endl;
    return input;
}

}
