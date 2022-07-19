#include "reader.hpp"

#include "builder.hpp"

#include <cassert>
#include <iostream>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>

#include "def.hpp"

#include "HConfig.h"  // for ZLIB_FOUND
#ifdef ZLIB_FOUND
#include "zstr.hpp"
#endif

enum class RawTokenType {
   NONE,
   STR,
   CONS,
   LESS,
   GREATER,
   EQUAL,
   COLON,
   LNEND,
   FLEND,
   BRKOP,
   BRKCL,
   PLUS,
   MINUS,
   HAT,
   SLASH,
   ASTERISK
};

struct RawToken {
   RawTokenType type = RawTokenType::NONE;
   std::string svalue;
   double dvalue = 0.0;

   inline bool istype(RawTokenType t) const {
      return this->type == t;
   }

   RawToken& operator=(RawTokenType t) {
      type = t;
      return *this;
   }
   RawToken& operator=(const std::string& v) {
      svalue = v;
      type = RawTokenType::STR;
      return *this;
   }
   RawToken& operator=(const double v) {
      dvalue = v;
      type = RawTokenType::CONS;
      return *this;
   }
};

enum class ProcessedTokenType {
   NONE,
   SECID,
   VARID,
   CONID,
   CONST,
   FREE,
   BRKOP,
   BRKCL,
   COMP,
   LNEND,
   SLASH,
   ASTERISK,
   HAT,
   SOSTYPE
};

enum class LpSectionKeyword {
  NONE,
  OBJMIN,
  OBJMAX,
  CON,
  BOUNDS,
  GEN,
  BIN,
  SEMI,
  SOS,
  END
};

enum class SosType {
   SOS1,
   SOS2
};

enum class LpComparisonType { LEQ, L, EQ, G, GEQ };

struct ProcessedToken {
   ProcessedTokenType type;
   union {
      LpSectionKeyword keyword;
      SosType sostype;
      char* name;
      double value;
      LpComparisonType dir;
   };

   ProcessedToken(const ProcessedToken&) = delete;
   ProcessedToken(ProcessedToken&& t)
   : type(t.type)
   {
      switch( type )
      {
         case ProcessedTokenType::SECID :
            keyword = t.keyword;
            break;
         case ProcessedTokenType::SOSTYPE :
            sostype = t.sostype;
            break;
         case ProcessedTokenType::CONID :
         case ProcessedTokenType::VARID :
            name = t.name;
            break;
         case ProcessedTokenType::CONST :
            value = t.value;
            break;
         case ProcessedTokenType::COMP :
            dir = t.dir;
            break;
         default: ;
      }
      t.type = ProcessedTokenType::NONE;
   }

   ProcessedToken(ProcessedTokenType t) : type(t) {};

   ProcessedToken(LpSectionKeyword kw) : type(ProcessedTokenType::SECID), keyword(kw) {};

   ProcessedToken(SosType sos) : type(ProcessedTokenType::SOSTYPE), sostype(sos) {};

   ProcessedToken(ProcessedTokenType t, const std::string& s) : type(t) {
      assert(t == ProcessedTokenType::CONID || t == ProcessedTokenType::VARID);
      name = strdup(s.c_str());
   };

   ProcessedToken(double v) : type(ProcessedTokenType::CONST), value(v) {};

   ProcessedToken(LpComparisonType comp) : type(ProcessedTokenType::COMP), dir(comp) {};

   ~ProcessedToken()
   {
      if( type == ProcessedTokenType::CONID || type == ProcessedTokenType::VARID )
         free(name);
   }
};

class Reader {
private:
#ifdef ZLIB_FOUND
   zstr::ifstream file;
#else
   std::ifstream file;
#endif
   std::string linebuffer;
   std::size_t linebufferpos;
   std::array<RawToken, 5> rawtokens;
   size_t rawtokenpos;
   std::vector<ProcessedToken> processedtokens;
   // store for each section a pointer to its begin and end (pointer to element after last)
   std::map<LpSectionKeyword, std::pair<std::vector<ProcessedToken>::iterator, std::vector<ProcessedToken>::iterator> > sectiontokens;

   Builder builder;

   bool readnexttoken(RawToken&);
   const RawToken& rawtoken(size_t offset = 0) const {
      assert(offset < 5);
      return rawtokens[(rawtokenpos + offset) % 5];
   }
   void nextrawtoken(size_t howmany = 1);
   void processtokens();
   void splittokens();
   void processsections();
   void processnonesec();
   void processobjsec();
   void processconsec();
   void processboundssec();
   void processbinsec();
   void processgensec();
   void processsemisec();
   void processsossec();
   void processendsec();
   void parseexpression(std::vector<ProcessedToken>::iterator& it, std::vector<ProcessedToken>::iterator end, std::shared_ptr<Expression> expr, bool isobj);

public:
   Reader(std::string filename) {
#ifdef ZLIB_FOUND
      try {
        file.open(filename);
      } catch ( const strict_fstream::Exception& e ) {
      }
#else
      file.open(filename);
#endif
      lpassert(file.is_open());
   };

   ~Reader() {
      file.close();
   }

   Model read();
};

Model readinstance(std::string filename) {
   Reader reader(filename);
   return reader.read();
}

static
bool iskeyword(const std::string str, const std::string* keywords, const int nkeywords) {
   for (int i=0; i<nkeywords; i++) {
      if (str == keywords[i]) {
         return true;
      }
   }
   return false;
}

static
LpSectionKeyword parsesectionkeyword(std::string str) {
   std::transform(str.begin(), str.end(), str.begin(),
      [](unsigned char c) { return std::tolower(c); });

   if (iskeyword(str, LP_KEYWORD_MIN, LP_KEYWORD_MIN_N)) {
      return LpSectionKeyword::OBJMIN;
   }

   if (iskeyword(str, LP_KEYWORD_MAX, LP_KEYWORD_MAX_N)) {
      return LpSectionKeyword::OBJMAX;
   }

   if (iskeyword(str, LP_KEYWORD_ST, LP_KEYWORD_ST_N)) {
      return LpSectionKeyword::CON;
   }

   if (iskeyword(str, LP_KEYWORD_BOUNDS, LP_KEYWORD_BOUNDS_N)) {
      return LpSectionKeyword::BOUNDS;
   }

   if (iskeyword(str, LP_KEYWORD_BIN, LP_KEYWORD_BIN_N)) {
      return LpSectionKeyword::BIN;
   }

   if (iskeyword(str, LP_KEYWORD_GEN, LP_KEYWORD_GEN_N)) {
      return LpSectionKeyword::GEN;
   }

   if (iskeyword(str, LP_KEYWORD_SEMI, LP_KEYWORD_SEMI_N)) {
      return LpSectionKeyword::SEMI;
   }

   if (iskeyword(str, LP_KEYWORD_SOS, LP_KEYWORD_SOS_N)) {
      return LpSectionKeyword::SOS;
   }

   if (iskeyword(str, LP_KEYWORD_END, LP_KEYWORD_END_N)) {
      return LpSectionKeyword::END;
   }

   return LpSectionKeyword::NONE;
}

Model Reader::read() {
   //std::clog << "Reading input, tokenizing..." << std::endl;
   this->linebufferpos = 0;
   this->rawtokenpos = 0;
   // read first 5 token
   // if file ends early, then all remaining tokens are set to FLEND
   for(size_t i = 0; i < 5; ++i )
      while( !readnexttoken(rawtokens[i]) ) ;;

   //std::clog << "Processing tokens..." << std::endl;
   processtokens();

   linebuffer.clear();
   linebuffer.shrink_to_fit();

   //std::clog << "Splitting tokens..." << std::endl;
   splittokens();

   //std::clog << "Setting up model..." << std::endl;
   processsections();
   processedtokens.clear();
   processedtokens.shrink_to_fit();

   return builder.model;
}

void Reader::processnonesec() {
   lpassert(sectiontokens.count(LpSectionKeyword::NONE) == 0);
}

void Reader::parseexpression(std::vector<ProcessedToken>::iterator& it, std::vector<ProcessedToken>::iterator end, std::shared_ptr<Expression> expr, bool isobj) {
   if(it != end && it->type == ProcessedTokenType::CONID) {
      expr->name = it->name;
      ++it;
   }

   while (it != end) {
      std::vector<ProcessedToken>::iterator next = it;
      ++next;
      // const var
      if (next != end && it->type == ProcessedTokenType::CONST  && next->type == ProcessedTokenType::VARID) {
         std::string name = next->name;
         
         std::shared_ptr<LinTerm> linterm = std::shared_ptr<LinTerm>(new LinTerm());
         linterm->coef = it->value;
         linterm->var = builder.getvarbyname(name);
         expr->linterms.push_back(linterm);

         ++it;
         ++it;
         continue;
      }

      // const
      if (it->type == ProcessedTokenType::CONST) {
         expr->offset += it->value;
         ++it;
         continue;
      }
      
      // var
      if (it->type == ProcessedTokenType::VARID) {
         std::string name = it->name;
         
         std::shared_ptr<LinTerm> linterm = std::shared_ptr<LinTerm>(new LinTerm());
         linterm->coef = 1.0;
         linterm->var = builder.getvarbyname(name);
         expr->linterms.push_back(linterm);

         ++it;
         continue;
      }

      // quadratic expression
      if (next != end && it->type == ProcessedTokenType::BRKOP) {
         ++it;
         while (it != end && it->type != ProcessedTokenType::BRKCL) {
            // const var hat const
            std::vector<ProcessedToken>::iterator next1 = it;  // token after it
            std::vector<ProcessedToken>::iterator next2 = it;  // token 2nd-after it
            std::vector<ProcessedToken>::iterator next3 = it;  // token 3rd-after it
            ++next1; ++next2; ++next3;
            if( next1 != end ) { ++next2; ++next3; }
            if( next2 != end ) ++next3;

            if (next3 != end
            && it->type == ProcessedTokenType::CONST
            && next1->type == ProcessedTokenType::VARID
            && next2->type == ProcessedTokenType::HAT
            && next3->type == ProcessedTokenType::CONST) {
               std::string name = next1->name;

               lpassert (next3->value == 2.0);

               std::shared_ptr<QuadTerm> quadterm = std::shared_ptr<QuadTerm>(new QuadTerm());
               quadterm->coef = it->value;
               quadterm->var1 = builder.getvarbyname(name);
               quadterm->var2 = builder.getvarbyname(name);
               expr->quadterms.push_back(quadterm);

               it = ++next3;
               continue;
            }

            // var hat const
            if (next2 != end
            && it->type == ProcessedTokenType::VARID
            && next1->type == ProcessedTokenType::HAT
            && next2->type == ProcessedTokenType::CONST) {
               std::string name = it->name;

               lpassert (next2->value == 2.0);

               std::shared_ptr<QuadTerm> quadterm = std::shared_ptr<QuadTerm>(new QuadTerm());
               quadterm->coef = 1.0;
               quadterm->var1 = builder.getvarbyname(name);
               quadterm->var2 = builder.getvarbyname(name);
               expr->quadterms.push_back(quadterm);

               it = next3;
               continue;
            }

            // const var asterisk var
            if (next3 != end
            && it->type == ProcessedTokenType::CONST
            && next1->type == ProcessedTokenType::VARID
            && next2->type == ProcessedTokenType::ASTERISK
            && next3->type == ProcessedTokenType::VARID) {
               std::string name1 = next1->name;
               std::string name2 = next3->name;

               std::shared_ptr<QuadTerm> quadterm = std::shared_ptr<QuadTerm>(new QuadTerm());
               quadterm->coef = it->value;
               quadterm->var1 = builder.getvarbyname(name1);
               quadterm->var2 = builder.getvarbyname(name2);
               expr->quadterms.push_back(quadterm);

               it = ++next3;
               continue;
            }

            // var asterisk var
            if (next2 != end
            && it->type == ProcessedTokenType::VARID
            && next1->type == ProcessedTokenType::ASTERISK
            && next2->type == ProcessedTokenType::VARID) {
               std::string name1 = it->name;
               std::string name2 = next2->name;

               std::shared_ptr<QuadTerm> quadterm = std::shared_ptr<QuadTerm>(new QuadTerm());
               quadterm->coef = 1.0;
               quadterm->var1 = builder.getvarbyname(name1);
               quadterm->var2 = builder.getvarbyname(name2);
               expr->quadterms.push_back(quadterm);

               it = next3;
               continue;
            }
            break;
         }
         if (isobj) {
           // only in the objective function, a quadratic term is followed by "/2.0"
           std::vector<ProcessedToken>::iterator next1 = it;  // token after it
           std::vector<ProcessedToken>::iterator next2 = it;  // token 2nd-after it
           ++next1; ++next2;
           if( next1 != end ) ++next2;

           lpassert(next2 != end);
           lpassert(it->type == ProcessedTokenType::BRKCL);
           lpassert(next1->type == ProcessedTokenType::SLASH);
           lpassert(next2->type == ProcessedTokenType::CONST);
           lpassert(next2->value == 2.0);
           it = ++next2;
         }
         else {
           lpassert(it != end);
           lpassert(it->type == ProcessedTokenType::BRKCL);
           ++it;
         }
         continue;
      }

      break;
   }
}

void Reader::processobjsec() {
   builder.model.objective = std::shared_ptr<Expression>(new Expression);
   if( sectiontokens.count(LpSectionKeyword::OBJMIN) )
   {
      builder.model.sense = ObjectiveSense::MIN;
      parseexpression(sectiontokens[LpSectionKeyword::OBJMIN].first, sectiontokens[LpSectionKeyword::OBJMIN].second, builder.model.objective, true);
      lpassert(sectiontokens[LpSectionKeyword::OBJMIN].first == sectiontokens[LpSectionKeyword::OBJMIN].second); // all section tokens should have been processed
   }
   else if( sectiontokens.count(LpSectionKeyword::OBJMAX) )
   {
      builder.model.sense = ObjectiveSense::MAX;
      parseexpression(sectiontokens[LpSectionKeyword::OBJMAX].first, sectiontokens[LpSectionKeyword::OBJMAX].second, builder.model.objective, true);
      lpassert(sectiontokens[LpSectionKeyword::OBJMAX].first == sectiontokens[LpSectionKeyword::OBJMAX].second); // all section tokens should have been processed
   }
}

void Reader::processconsec() {
   if(!sectiontokens.count(LpSectionKeyword::CON))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::CON].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::CON].second);
   while (begin != end) {
      std::shared_ptr<Constraint> con = std::shared_ptr<Constraint>(new Constraint);
      parseexpression(begin, end, con->expr, false);
      // should not be at end of section yet, but a comparison operator should be next
      lpassert(begin != sectiontokens[LpSectionKeyword::CON].second);
      lpassert(begin->type == ProcessedTokenType::COMP);
      LpComparisonType dir = begin->dir;
      ++begin;

      // should still not be at end of section yet, but a right-hand-side value should be next
      lpassert(begin != sectiontokens[LpSectionKeyword::CON].second);
      lpassert(begin->type == ProcessedTokenType::CONST);
      switch (dir) {
         case LpComparisonType::EQ:
            con->lowerbound = con->upperbound = begin->value;
            break;
         case LpComparisonType::LEQ:
            con->upperbound = begin->value;
            break;
         case LpComparisonType::GEQ:
            con->lowerbound = begin->value;
            break;
         default:
            lpassert(false);
      }
      builder.model.constraints.push_back(con);
      ++begin;
   }
}

void Reader::processboundssec() {
   if(!sectiontokens.count(LpSectionKeyword::BOUNDS))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::BOUNDS].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::BOUNDS].second);
   while (begin != end) {
      std::vector<ProcessedToken>::iterator next1 = begin;  // token after begin

      // VAR free
      if (next1 != end
         && begin->type == ProcessedTokenType::VARID
         && next1->type == ProcessedTokenType::FREE) {
         std::string name = begin->name;
         std::shared_ptr<Variable> var = builder.getvarbyname(name);
         var->lowerbound = -std::numeric_limits<double>::infinity(); 
         var->upperbound = std::numeric_limits<double>::infinity();
         begin = ++next1;
		 continue;
      }

      std::vector<ProcessedToken>::iterator next2 = begin;  // token 2nd-after begin
      std::vector<ProcessedToken>::iterator next3 = begin;  // token 3rd-after begin
      std::vector<ProcessedToken>::iterator next4 = begin;  // token 4th-after begin
      ++next1; ++next2; ++next3; ++next4;
      if( next1 != end ) { ++next2; ++next3; ++next4; }
      if( next2 != end ) { ++next3; ++next4; }
      if( next3 != end ) ++next4;

	  // CONST COMP VAR COMP CONST
	  if (next4 != end
		  && begin->type == ProcessedTokenType::CONST
		  && next1->type == ProcessedTokenType::COMP
		  && next2->type == ProcessedTokenType::VARID
		  && next3->type == ProcessedTokenType::COMP
		  && next4->type == ProcessedTokenType::CONST) {
		  lpassert(next1->dir == LpComparisonType::LEQ);
		  lpassert(next3->dir == LpComparisonType::LEQ);

		  double lb = begin->value;
		  double ub = next4->value;

		  std::string name = next2->name;
		  std::shared_ptr<Variable> var = builder.getvarbyname(name);

		  var->lowerbound = lb;
		  var->upperbound = ub;

		  begin = ++next4;
		  continue;
	  }

      // CONST COMP VAR
      if (next2 != end
      && begin->type == ProcessedTokenType::CONST
      && next1->type == ProcessedTokenType::COMP
      && next2->type == ProcessedTokenType::VARID) {
         double value = begin->value;
         std::string name = next2->name;
         std::shared_ptr<Variable> var = builder.getvarbyname(name);
         LpComparisonType dir = next1->dir;

         lpassert(dir != LpComparisonType::L && dir != LpComparisonType::G);

         switch (dir) {
            case LpComparisonType::LEQ:
               var->lowerbound = value;
               break;
            case LpComparisonType::GEQ:
               var->upperbound = value;
               break;
            case LpComparisonType::EQ:
               var->lowerbound = var->upperbound = value;
               break;
            default:
               lpassert(false);
         }
         begin = next3;
         continue;
      }

      // VAR COMP CONST
      if (next2 != end
      && begin->type == ProcessedTokenType::VARID
      && next1->type == ProcessedTokenType::COMP
      && next2->type == ProcessedTokenType::CONST) {
         double value = next2->value;
         std::string name = begin->name;
         std::shared_ptr<Variable> var = builder.getvarbyname(name);
         LpComparisonType dir = next1->dir;

         lpassert(dir != LpComparisonType::L && dir != LpComparisonType::G);

         switch (dir) {
            case LpComparisonType::LEQ:
               var->upperbound = value;
               break;
            case LpComparisonType::GEQ:
               var->lowerbound = value;
               break;
            case LpComparisonType::EQ:
               var->lowerbound = var->upperbound = value;
               break;
            default:
               lpassert(false);
         }
         begin = next3;
         continue;
      }
      
	  lpassert(false);
   }
}

void Reader::processbinsec() {
   if(!sectiontokens.count(LpSectionKeyword::BIN))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::BIN].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::BIN].second);
   for (; begin != end; ++begin) {
      lpassert(begin->type == ProcessedTokenType::VARID);
      std::string name = begin->name;
      std::shared_ptr<Variable> var = builder.getvarbyname(name);
      var->type = VariableType::BINARY;
      var->lowerbound = 0.0;
      var->upperbound = 1.0;
   }
}

void Reader::processgensec() {
   if(!sectiontokens.count(LpSectionKeyword::GEN))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::GEN].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::GEN].second);
   for (; begin != end; ++begin) {
      lpassert(begin->type == ProcessedTokenType::VARID);
      std::string name = begin->name;
      std::shared_ptr<Variable> var = builder.getvarbyname(name);
      if (var->type == VariableType::SEMICONTINUOUS) {
         var->type = VariableType::SEMIINTEGER;
      } else {
         var->type = VariableType::GENERAL;
      }
   }
}

void Reader::processsemisec() {
   if(!sectiontokens.count(LpSectionKeyword::GEN))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::SEMI].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::SEMI].second);
   for (; begin != end; ++begin) {
      lpassert(begin->type == ProcessedTokenType::VARID);
      std::string name = begin->name;
      std::shared_ptr<Variable> var = builder.getvarbyname(name);
      if (var->type == VariableType::GENERAL) {
         var->type = VariableType::SEMIINTEGER;
      } else {
         var->type = VariableType::SEMICONTINUOUS;
      }
   }
}

void Reader::processsossec() {
   if(!sectiontokens.count(LpSectionKeyword::SOS))
      return;
   std::vector<ProcessedToken>::iterator& begin(sectiontokens[LpSectionKeyword::SOS].first);
   std::vector<ProcessedToken>::iterator& end(sectiontokens[LpSectionKeyword::SOS].second);
   while (begin != end) {
      std::shared_ptr<SOS> sos = std::shared_ptr<SOS>(new SOS);

      // sos1: S1 :: x1 : 1  x2 : 2  x3 : 3

      // name of SOS is mandatory
      lpassert(begin->type == ProcessedTokenType::CONID);
      sos->name = begin->name;
      ++begin;

      // SOS type
      lpassert(begin != end);
      lpassert(begin->type == ProcessedTokenType::SOSTYPE);
      sos->type = begin->sostype == SosType::SOS1 ? 1 : 2;
      ++begin;

      while (begin != end) {
         // process all "var : weight" entries
         // when processtokens() sees a string followed by a colon, it classifies this as a CONID
         // but in a SOS section, this is actually a variable identifier
         if (begin->type != ProcessedTokenType::CONID)
            break;
         std::string name = begin->name;
         ++begin;
         if (begin != end && begin->type == ProcessedTokenType::CONST) {
            auto var = builder.getvarbyname(name);
            double weight = begin->value;

            sos->entries.push_back({var, weight});

            ++begin;
            continue;
         }

         break;
      }

      builder.model.soss.push_back(sos);
   }
}

void Reader::processendsec() {
   lpassert(sectiontokens.count(LpSectionKeyword::END) == 0);
}

void Reader::processsections() {
   processnonesec();
   processobjsec();
   processconsec();
   processboundssec();
   processgensec();
   processbinsec();
   processsemisec();
   processsossec();
   processendsec();
}

void Reader::splittokens() {
   LpSectionKeyword currentsection = LpSectionKeyword::NONE;
   
   for (std::vector<ProcessedToken>::iterator it(processedtokens.begin()); it != processedtokens.end(); ++it)
      if (it->type == ProcessedTokenType::SECID) {
         if(currentsection != LpSectionKeyword::NONE)
            sectiontokens[currentsection].second = it;  // mark end of previous section
         currentsection = it->keyword;

         // make sure this section did not yet occur
         lpassert(sectiontokens.count(currentsection) == 0);

         std::vector<ProcessedToken>::iterator next = it;
         ++next;
         // skip empty section
         if( next == processedtokens.end() || next->type == ProcessedTokenType::SECID ) {
            currentsection = LpSectionKeyword::NONE;
            continue;
         }
         // remember begin of new section: its the token following the current one
         sectiontokens[currentsection].first = next;
      }

   if(currentsection != LpSectionKeyword::NONE)
      sectiontokens[currentsection].second = processedtokens.end();  // mark end of last section
}

void Reader::processtokens() {
   while(!rawtoken().istype(RawTokenType::FLEND)) {
      fflush(stdout);

      // Slash + asterisk: comment, skip everything up to next asterisk + slash
      if (rawtoken().istype(RawTokenType::SLASH) && rawtoken(1).istype(RawTokenType::ASTERISK)) {
         do
         {
            nextrawtoken(2);
         }
         while( !(rawtoken().istype(RawTokenType::ASTERISK) && rawtoken(1).istype(RawTokenType::SLASH)) && !rawtoken().istype(RawTokenType::FLEND) );
         nextrawtoken(2);
         continue;
      }

      // long section keyword semi-continuous
      if (rawtoken().istype(RawTokenType::STR) && rawtoken(1).istype(RawTokenType::MINUS) && rawtoken(2).istype(RawTokenType::STR)) {
         std::string temp = rawtoken().svalue + "-" + rawtoken(2).svalue;
         LpSectionKeyword keyword = parsesectionkeyword(temp);
         if (keyword != LpSectionKeyword::NONE) {
            processedtokens.emplace_back(keyword);
            nextrawtoken(3);
            continue;
         }
      }

      // long section keyword subject to/such that
      if (rawtoken().istype(RawTokenType::STR) && rawtoken(1).istype(RawTokenType::STR)) {
         std::string temp = rawtoken().svalue + " " + rawtoken(1).svalue;
         LpSectionKeyword keyword = parsesectionkeyword(temp);
         if (keyword != LpSectionKeyword::NONE) {
            processedtokens.emplace_back(keyword);
            nextrawtoken(2);
            continue;
         }
      }

      // other section keyword
      if (rawtoken().istype(RawTokenType::STR)) {
         LpSectionKeyword keyword = parsesectionkeyword(rawtoken().svalue);
         if (keyword != LpSectionKeyword::NONE) {
            processedtokens.emplace_back(keyword);
            nextrawtoken();
            continue;
         }
      }

      // sos type identifier? "S1 ::" or "S2 ::"
      if (rawtoken().istype(RawTokenType::STR) && rawtoken(1).istype(RawTokenType::COLON) && rawtoken(2).istype(RawTokenType::COLON)) {
         lpassert(rawtoken().svalue.length() == 2);
         lpassert(rawtoken().svalue[0] == 'S' || rawtoken().svalue[0] == 's');
         lpassert(rawtoken().svalue[1] == '1' || rawtoken().svalue[1] == '2');
         processedtokens.emplace_back(rawtoken().svalue[1] == '1' ? SosType::SOS1 : SosType::SOS2);
         nextrawtoken(3);
         continue;
      }

      // constraint identifier?
      if (rawtoken().istype(RawTokenType::STR) && rawtoken(1).istype(RawTokenType::COLON)) {
         processedtokens.emplace_back(ProcessedTokenType::CONID, rawtoken().svalue);
         nextrawtoken(2);
         continue;
      }

      // check if free
      if (rawtoken().istype(RawTokenType::STR) && iskeyword(rawtoken().svalue, LP_KEYWORD_FREE, LP_KEYWORD_FREE_N)) {
         processedtokens.emplace_back(ProcessedTokenType::FREE);
         nextrawtoken();
         continue;
      }

      // check if infinity
      if (rawtoken().istype(RawTokenType::STR) && iskeyword(rawtoken().svalue, LP_KEYWORD_INF, LP_KEYWORD_INF_N)) {
         processedtokens.emplace_back(std::numeric_limits<double>::infinity());
         nextrawtoken();
         continue;
      }

      // assume var identifier
      if (rawtoken().istype(RawTokenType::STR)) {
         processedtokens.emplace_back(ProcessedTokenType::VARID, rawtoken().svalue);
         nextrawtoken();
         continue;
      }

      // + Constant
      if (rawtoken().istype(RawTokenType::PLUS) && rawtoken(1).istype(RawTokenType::CONS)) {
         processedtokens.emplace_back(rawtoken(1).dvalue);
         nextrawtoken(2);
         continue;
      }

      // - constant
      if (rawtoken().istype(RawTokenType::MINUS) && rawtoken(1).istype(RawTokenType::CONS)) {
         processedtokens.emplace_back(-rawtoken(1).dvalue);
         nextrawtoken(2);
         continue;
      }

      // + [
      if (rawtoken().istype(RawTokenType::PLUS) && rawtoken(1).istype(RawTokenType::BRKOP)) {
         processedtokens.emplace_back(ProcessedTokenType::BRKOP);
         nextrawtoken(2);
         continue;
      }

      // - [
      if (rawtoken().istype(RawTokenType::MINUS) && rawtoken(1).istype(RawTokenType::BRKOP)) {
         lpassert(false);
      }

      // constant [
      if (rawtoken().istype(RawTokenType::CONS) && rawtoken(1).istype(RawTokenType::BRKOP)) {
         lpassert(false);
      }

      // +
      if (rawtoken().istype(RawTokenType::PLUS)) {
         processedtokens.emplace_back(1.0);
         nextrawtoken();
         continue;
      }

      // -
      if (rawtoken().istype(RawTokenType::MINUS)) {
         processedtokens.emplace_back(-1.0);
         nextrawtoken();
         continue;
      }

      // constant
      if (rawtoken().istype(RawTokenType::CONS)) {
         processedtokens.emplace_back(rawtoken().dvalue);
         nextrawtoken();
         continue;
      }

      // [
      if (rawtoken().istype(RawTokenType::BRKOP)) {
         processedtokens.emplace_back(ProcessedTokenType::BRKOP);
         nextrawtoken();
         continue;
      }

      // ]
      if (rawtoken().istype(RawTokenType::BRKCL)) {
         processedtokens.emplace_back(ProcessedTokenType::BRKCL);
         nextrawtoken();
         continue;
      }

      // /
      if (rawtoken().istype(RawTokenType::SLASH)) {
         processedtokens.emplace_back(ProcessedTokenType::SLASH);
         nextrawtoken();
         continue;
      }

      // *
      if (rawtoken().istype(RawTokenType::ASTERISK)) {
         processedtokens.emplace_back(ProcessedTokenType::ASTERISK);
         nextrawtoken();
         continue;
      }

      // ^
      if (rawtoken().istype(RawTokenType::HAT)) {
         processedtokens.emplace_back(ProcessedTokenType::HAT);
         nextrawtoken();
         continue;
      }

      // <=
      if (rawtoken().istype(RawTokenType::LESS) && rawtoken(1).istype(RawTokenType::EQUAL)) {
         processedtokens.emplace_back(LpComparisonType::LEQ);
         nextrawtoken(2);
         continue;
      }

      // <
      if (rawtoken().istype(RawTokenType::LESS)) {
         processedtokens.emplace_back(LpComparisonType::L);
         nextrawtoken();
         continue;
      }

      // >=
      if (rawtoken().istype(RawTokenType::GREATER) && rawtoken(1).istype(RawTokenType::EQUAL)) {
         processedtokens.emplace_back(LpComparisonType::GEQ);
         nextrawtoken(2);
         continue;
      }

      // >
      if (rawtoken().istype(RawTokenType::GREATER)) {
         processedtokens.emplace_back(LpComparisonType::G);
         nextrawtoken();
         continue;
      }

      // =
      if (rawtoken().istype(RawTokenType::EQUAL)) {
         processedtokens.emplace_back(LpComparisonType::EQ);
         nextrawtoken();
         continue;
      }

      // FILEEND should have been handled in condition of while()
      assert(!rawtoken().istype(RawTokenType::FLEND));

      // catch all unknown symbols
      lpassert(false);
      break;
   }
}

void Reader::nextrawtoken(size_t howmany) {
   assert(howmany > 0);
   while( howmany-- )
   {
      // call readnexttoken() to overwrite current token
      // if it didn't actually read a token (returns false), then call again
      while( !readnexttoken(rawtokens[rawtokenpos % 5]) ) ;;
      ++rawtokenpos;
   }
}

// return true, if token has been set; return false if skipped over whitespace only
bool Reader::readnexttoken(RawToken& t) {
   if (this->linebufferpos == this->linebuffer.size()) {
     // read next line if any are left. 
     if (this->file.eof()) {
         t = RawTokenType::FLEND;
         return true;
     }
     std::getline(this->file, linebuffer);

     // drop \r
     if (!linebuffer.empty() && linebuffer.back() == '\r')
        linebuffer.pop_back();

     // reset linebufferpos
     this->linebufferpos = 0;
   }

   // check single character tokens
   char nextchar = this->linebuffer[this->linebufferpos];

   switch (nextchar) {
      // check for comment
      case '\\':
         // skip rest of line
         this->linebufferpos = this->linebuffer.size();
         return false;
      
      // check for bracket opening
      case '[':
         t = RawTokenType::BRKOP;
         this->linebufferpos++;
         return true;

      // check for bracket closing
      case ']':
         t = RawTokenType::BRKCL;
         this->linebufferpos++;
         return true;

      // check for less sign
      case '<':
         t = RawTokenType::LESS;
         this->linebufferpos++;
         return true;

      // check for greater sign
      case '>':
         t = RawTokenType::GREATER;
         this->linebufferpos++;
         return true;

      // check for equal sign
      case '=':
         t = RawTokenType::EQUAL;
         this->linebufferpos++;
         return true;
      
      // check for colon
      case ':':
         t = RawTokenType::COLON;
         this->linebufferpos++;
         return true;

      // check for plus
      case '+':
         t = RawTokenType::PLUS;
         this->linebufferpos++;
         return true;

      // check for hat
      case '^':
         t = RawTokenType::HAT;
         this->linebufferpos++;
         return true;

      // check for slash
      case '/':
         t = RawTokenType::SLASH;
         this->linebufferpos++;
         return true;

      // check for asterisk
      case '*':
         t = RawTokenType::ASTERISK;
         this->linebufferpos++;
         return true;
      
      // check for minus
      case '-':
         t = RawTokenType::MINUS;
         this->linebufferpos++;
         return true;

      // check for whitespace
      case ' ':
      case '\t':
         this->linebufferpos++;
         return false;

      // check for line end
      case ';':
      case '\n':  // \n should not happen due to using getline()
         this->linebufferpos = this->linebuffer.size();
         return false;

      case '\0':  // empty line
         assert(this->linebufferpos == this->linebuffer.size());
         return false;
   }

   // check for double value
   const char* startptr = this->linebuffer.data()+this->linebufferpos;
   char* endptr;
   double constant = strtod(startptr, &endptr);
   if (endptr != startptr) {
      t = constant;
      this->linebufferpos += endptr - startptr;
      return true;
   }

   // assume it's an (section/variable/constraint) identifier
   auto endpos = this->linebuffer.find_first_of("\t\n\\:+<>^= /-*", this->linebufferpos);
   if( endpos == std::string::npos )
      endpos = this->linebuffer.size();  // take complete rest of string
   if( endpos > this->linebufferpos ) {
      t = std::string(this->linebuffer, this->linebufferpos, endpos - this->linebufferpos);
      this->linebufferpos = endpos;
      return true;
   }
   
   lpassert(false);
   return false;
}
