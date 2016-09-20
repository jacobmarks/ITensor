//
// Distributed under the ITensor Library License, Version 1.2
//    (See accompanying LICENSE file.)
//
#include <algorithm>
#include <map>
#include "itensor/util/print_macro.h"
#include "itensor/mps/autompo.h"
#include "itensor/tensor/algs.h"

using std::find;
using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::array;
using std::pair;
using std::make_pair;
using std::move;
using std::min;
using std::max;
using std::map;
using std::set;

namespace itensor {

bool
isZero(Cplx const& z, Real thresh = 1E-13) { return std::abs(z) < thresh; }

bool
less(Real x, Real y, Real eps = 1E-12)
    {
    Real ax = std::fabs(x);
    Real ay = std::fabs(y);
    Real scale = (ax < ay ? ay : ax);
    return (y-x) >= scale*eps;
    }

bool
equal(Cplx x, Cplx y, Real eps = 1E-12)
    {
    Real ax = std::abs(x);
    Real ay = std::abs(y);
    Real scale = (ax < ay ? ay : ax);
    return std::abs(x-y) <= scale*eps;
    }

bool
less(Cplx const& z1, Cplx const& z2, Real eps = 1E-12) 
    { 
    if(not equal(z1.real(),z2.real(),eps)) 
        {
        return less(z1.real(),z2.real(),eps);
        }
    return less(z1.imag(),z2.imag());
    }

bool
isReal(const Cplx& z) { return z.imag() == 0; }

bool
isApproxReal(Cplx const& z, Real epsilon = 1E-12) { return std::fabs(z.imag()) < epsilon; }

SiteTerm::
SiteTerm() : i(-1) { }

SiteTerm::
SiteTerm(string const& op_,
         int i_)
    :
    op(op_),
    i(i_)
    { }

bool
isFermionic(SiteTerm const& st)
    {
#ifdef DEBUG
    for(char c : st.op)
    if(c == '*')
        {
        Print(st.op);
        Error("SiteTerm contains a '*' but isFermionic does not handle this case");
        }
#endif
    if(!st.op.empty() && st.op.front() == 'C') return true;
    return false;
    }

SiteTermProd 
mult(SiteTermProd first, 
     SiteTermProd const& second)
    {
    first.insert(first.end(), second.begin(), second.end());
    return first;
    }    

bool
isFermionic(SiteTermProd const& sprod)
    {
    bool isf = false;
    for(auto& st : sprod)
        {
        //Flip isf in a Z2 fashion for every fermionic operator
        if(isFermionic(st)) isf = !isf;
        }
    return isf;
    }

string
fermionicTerm(const string& op)
    {
    static array<pair<string,string>,6>
           rewrites =
           {{
           make_pair("Cdagup","Adagup"),
           make_pair("Cup","Aup"),
           make_pair("Cdagdn","Adagdn*Fup"),
           make_pair("Cdn","Adn*Fup"),
           make_pair("C","A"),
           make_pair("Cdag","Adag")
           }};
    for(auto& p : rewrites)
        {
        if(p.first == op) return p.second;
        }
    return op;
    }

void 
rewriteFermionic(SiteTermProd & prod, 
                 bool isleftFermionic)
    {
    if(prod.empty()) Error("Empty product in rewriteFermionic is not expected.");    
    
    int i = prod.front().i;
    for(auto& st : prod)
        if(st.i != i)
            {
            Error("Multi-site product in rewriteFermionic is not expected.");    
            }

    // Rewrite a fermionic single site product using the Jordan-Wigner string            
    bool isSiteFermionic = isFermionic(prod);
    if(isSiteFermionic)
        {
        for(auto& st : prod) if(isFermionic(st)) st.op = fermionicTerm(st.op);
        }
    
    // Add a FermiPhase operator at the end if the product of operators
    // to the left (including this site) is fermionic
    if((isleftFermionic && !isSiteFermionic) || (!isleftFermionic && isSiteFermionic))
        {
        prod.emplace_back("F", i);         
        }
    }

IQTensor
computeProd(SiteSet const& sites, 
            SiteTermProd const& p)
    {
    auto i = p.front().i;
    auto op = sites.op(p.front().op,i);
    for(auto it = p.begin()+1; it != p.end(); ++it)
        {
        if(it->i != i) Error("Op on wrong site");
        op = multSiteOps(op,sites.op(it->op,i));
        }
    return op;
    }

void HTerm::
add(string const& op,
    int i,
    Real x)
    {
    //The following ensures operators remain
    //in site order within the vector "ops"
    auto it = ops.begin();
    while(it != ops.end() && it->i <= i) ++it;

    auto t = SiteTerm(op,i);

    // If the operator is fermionic and being inserted in between existing operators 
    // need to check if an extra minus is required
    if(it != ops.end() && isFermionic(t))
        { 
        auto rightOps = SiteTermProd(it,ops.end());
        if(isFermionic(rightOps)) coef *= -1;
        }   

    coef *= x;
    ops.insert(it,t);
    }

HTerm& HTerm::
operator*=(Real x)
    {
    if(Nops() == 0) Error("No operators in HTerm");
    coef *= x;
    return *this;
    }

HTerm& HTerm::
operator*=(Complex x)
    {
    if(Nops() == 0) Error("No operators in HTerm");
    coef *= x;
    return *this;
    }

bool HTerm::
operator==(HTerm const& o) const
    {
    if(not equal(coef,o.coef,1E-12)) return false;
    if(Nops() != o.Nops()) return false;

    for(size_t n = 0; n <= ops.size(); ++n)
    if(ops[n] != o.ops.at(n)) 
        {
        return false;
        }

    return true;
    }

bool HTerm::
operator<(HTerm const& o) const 
    { 
    if(not equal(coef,o.coef,1E-12)) return less(coef,o.coef,1E-12);
    return (ops < o.ops);
    }

bool LessNoCoef::
operator()(HTerm const& t1, HTerm const& t2) const
    { 
    if(t1.ops.size() != t2.ops.size()) return t1.ops.size() < t2.ops.size();
            
    for(size_t j = 0ul; j < t1.ops.size(); ++j)
        {
        if(t1.ops[j] != t2.ops[j]) return t1.ops[j] < t2.ops[j];
        }
    return false;
    }

AutoMPO::Accumulator::
Accumulator(AutoMPO* pa_, 
            Real x_)
    :
    pa(pa_),
    state(New),
    coef(x_)
    {}

AutoMPO::Accumulator::
Accumulator(AutoMPO* pa_, 
            Complex x_)
    :
    pa(pa_),
    state(New),
    coef(x_)
    {}

AutoMPO::Accumulator::
Accumulator(AutoMPO* pa_)
    : 
    Accumulator(pa_,1)
    {}


AutoMPO::Accumulator::
Accumulator(AutoMPO* pa_, 
            const char* op_)
    :
    pa(pa_),
    state(Op),
    coef(1),
    op(op_)
    {}

AutoMPO::Accumulator::
Accumulator(AutoMPO* pa_, 
            const string& op_)
    :
    pa(pa_),
    state(Op),
    coef(1),
    op(op_)
    {}


AutoMPO::Accumulator::
~Accumulator()
    {
    if(state==Op) Error("Invalid input to AutoMPO (missing site number?)");
    term *= coef;
    pa->add(term);
    }
    

AutoMPO::Accumulator& AutoMPO::Accumulator::
operator,(Real x)
    {
    coef *= x;
    return *this;
    }

AutoMPO::Accumulator& AutoMPO::Accumulator::
operator,(Complex x)
    {
    coef *= x;
    return *this;
    }

AutoMPO::Accumulator& AutoMPO::Accumulator::
operator,(int i)
    {
    if(state==Op)
        {
        term.add(op,i);
        state = New;
        op = "";
        }
    else
        {
        coef *= Real(i);
        }
    return *this;
    }

AutoMPO::Accumulator& AutoMPO::Accumulator::
operator,(const char* op_)
    {
    if(state == New)
        {
        op = op_;
        state = Op;
        }
    else
        {
        Error("Invalid input to AutoMPO (two strings in a row?)");
        }
    return *this;
    }

AutoMPO::Accumulator& AutoMPO::Accumulator::
operator,(const string& op_)
    {
    if(state == New)
        {
        op = op_;
        state = Op;
        }
    else
        {
        Error("Invalid input to AutoMPO (two strings in a row?)");
        }
    return *this;
    }

void AutoMPO::
add(HTerm const& t)
    {
    if(abs(t.coef) == 0.0) return;

    auto it = terms_.find(t);
    if(it == terms_.end())
        {
        terms_.insert(move(t));
        }
    else //found duplicate
        {
        auto nt = t;
        nt.coef += it->coef;
        terms_.erase(it);
        terms_.insert(move(nt));
        }
    }

/*
MPO convention:
===============
For each link of the MPO, define a set of bases 
that describe the terms of the Hamiltonian
corresponding to the left "half" of the MPO.
The terms include "IL", which means the product
of identities to the left, and "HL", the sum of
all terms entirely contained on the left.

Usually these two special terms occupy positions 1 and two,
respectively.

The rest of the bases are each site term on the left that
is connected to something on the right.

So for neighbor and next neighbor, operator pair A B, 
coefs t1 and t2, on site n, the MPO matrix is:
n-1             n
      1111   HL  11A1  111A  <== bases
1111   1     0     0    A         
HL     0     1     0    0   
11A1   0    t2 B   0    0   
111A   0    t1 B   1    0   

For neighbor and next neighbor, operator pair A B and B A, t1 and t2
site n:
n-1             n
      1111  HL    11A1 11B1  111A  111B 
1111   1     0     0     0     A     B  
HL     0     1     0     0     0     0  
11A1   0    t2 B   0     0     0     0  
11B1   0    t2 A   0     0     0     0  
111A   0    t1 B   1     0     0     0  
111B   0    t1 A   0     1     0     0  

F == fermiPhase, i.e. F = (-1)^(# of fermions of either type of spin)
Then we make c and cdagger both have F's going off to the left.

Fermion operator rewriting convention:

//
//Spinless fermions
//

Cdag_i C_j  = (F_1 F_2 F_3 ... F_{i-1})^2 (Adag_i F_i) F_{i+1} ... A_j
            = Adag_i F_{i+1} ... A_j

C_i Cdag_j = (A_i F_i) F_{i+1} ... Adag_j

//
//Fermions with spin
//

Cdagup_i Cup_j  = (F_1 F_2 F_3 ... )^2 (Adagup_i F_i) F_{i+1} ... Aup_j
                = (Adagup_i F_i) F_{i+1} ... Aup_j //cancel squared F operators

Cup_i Cdagup_j = (Aup_i F_i) F_{i+1} ... Adagup_j

Cdagdn_i Cdn_j  = (Adagdn_i F_i) F_{i+1} ... Fup_j Adn_j 
                = - Adagdn_i F_{i+1} ... Fup_j Adn_j     //use Adagdn_i * F_i = -Adagdn_i
                = Adagdn_i F_{i+1} ... Fup_j Fdn_j Adn_j //use Adn_j = -Fdn_j*Adn_j
                = Adagdn_i F_{i+1} ... (F_j Adn_j)       //combine Fup_j*Fdn_j = F_j (definition)

Cdn_i Cdagdn_j = (Adn_i F_i) F_{i+1} ... Fup_j Adagdn_j
               = - Adn_i F_{i+1} ... Fup_j Adagdn_j      //use Adn_i*F_i = -Adn_i
               = Adn_i F_{i+1} ... Fup_j Fdn_j Adagdn_j  //use Adagdn_j = -Fdn_j*Adagdn_j
               = Adn_i F_{i+1} ... (F_j Adagdn_j)        //combined Fup_j*Fdn_j = F_j (definition)


*/


//TODO:
// o Add support for > 2 site operators
// o Add support for long-range (exponentially-decaying type) operator strings
// o Add support for fermionic operator strings

struct SiteQN
    {
    SiteTerm st;
    QN q;

    SiteQN() { }

    SiteQN(SiteTerm const& st_,
           QN const& q_)
      : st(st_),
        q(q_)
        { }
    };

std::ostream&
operator<<(std::ostream & s, SiteQN const& sq)
    {
    s << "SiteQN: " << sq.st << ", " << sq.q;
    return s;
    }

void
plusAppend(string & s, string const& a)
    {
    if(s.size() == 0 || s == "0") s = a;
    else 
        {
        s += "+";
        s += a;
        }
    }

//#define SHOW_AUTOMPO


string
startTerm(const string& op)
    {
    static array<pair<string,string>,6>
           rewrites =
           {{
           make_pair("Cdagup","Adagup*F"),
           make_pair("Cup","Aup*F"),
           make_pair("Cdagdn","Adagdn"),
           make_pair("Cdn","Adn"),
           make_pair("C","A*F"),
           make_pair("Cdag","Adag")
           }};
    for(auto& p : rewrites)
        {
        if(p.first == op) return p.second;
        }
    return op;
    }

string
endTerm(const string& op)
    {
    static array<pair<string,string>,6>
           rewrites =
           {{
           make_pair("Cup","Aup"),
           make_pair("Cdagup","Adagup"),
           make_pair("Cdn","F*Adn"),
           make_pair("Cdagdn","F*Adagdn"),
           make_pair("C","A"),
           make_pair("Cdag","Adag")
           }};
    for(auto& p : rewrites)
        {
        if(p.first == op) return p.second;
        }
    return op;
    }

template<typename Tensor>
MPOt<Tensor>
toMPOImpl(AutoMPO const& am,
          Args const& args)
    {
    using IndexT = typename Tensor::index_type;
    auto checkqn = args.getBool("CheckQN",true);

    auto const& sites = am.sites();
    auto H = MPOt<Tensor>(sites);
    auto N = sites.N();

    for(auto& t : am.terms())
    if(t.Nops() > 2) 
        {
        Error("Only at most 2-operator terms allowed for exact AutoMPO conversion to MPO/IQMPO");
        }

    //Special SiteTerm objects indicating either
    //a string of identities coming from the first
    //site of the system or the completed Hamitonian
    //for the left-hand side of the system
    auto IL = SiteTerm("IL",0);
    auto HL = SiteTerm("HL",0);

    auto basis = vector<vector<SiteQN>>(N+1);
    for(int n = 0; n < N; ++n)  
        {
        basis.at(n).emplace_back(IL,QN());
        }
    for(int n = 1; n <= N; ++n) 
        {
        basis.at(n).emplace_back(HL,QN());
        }

    const auto Zero = QN{};

    //Fill up the basis array at each site with 
    //the unique operator types occurring on the site
    //(unique including their coefficient)
    //and starting a string of operators (i.e. first op of an HTerm)
    for(auto& ht : am.terms())
        {
        for(auto n = ht.first().i; n <= ht.last().i; ++n)
            {
            auto& bn = basis.at(n);
            auto test_has_first = [&ht](SiteQN const& sq){ return sq.st == ht.first(); };
            bool has_first = (stdx::find_if(bn,test_has_first) != bn.end());
            if(!has_first) 
                {
                auto Op = sites.op(ht.first().op,ht.first().i);
                //printfln("Adding Op to basis at %d, Op=\n%s",n,Op);
                if(checkqn)
                    {
                    bn.emplace_back(ht.first(),-div(Op));
                    }
                else
                    {
                    bn.emplace_back(ht.first(),Zero);
                    }
                }
            }
        }

    if(checkqn)
        {
        auto qn_comp = [&Zero](const SiteQN& sq1,const SiteQN& sq2)
                       {
                       //First two if statements are to artificially make
                       //the default-constructed Zero QN come first in the sort
                       if(sq1.q == Zero && sq2.q != Zero) return true;
                       else if(sq2.q == Zero && sq1.q != Zero) return false;
                       return sq1.q < sq2.q;
                       };
        //Sort bond "basis" elements by quantum number sector:
        for(auto& bn : basis) std::sort(bn.begin(),bn.end(),qn_comp);
        }

    auto links = vector<IndexT>(N+1);
    auto inqn = vector<IndexQN>{};
    for(int n = 0; n <= N; ++n)
        {
        auto& bn = basis.at(n);
        inqn.clear();
        QN currq = bn.front().q;
        int currm = 0;
        int count = 0;
        for(auto& sq : bn)
            {
            if(sq.q == currq)
                {
                ++currm;
                }
            else
                {
                inqn.emplace_back(Index(format("hl%d_%d",n,count++),currm),currq);
                currq = sq.q;
                currm = 1;
                }
            }
        inqn.emplace_back(Index(format("hl%d_%d",n,count++),currm),currq);

        links.at(n) = IQIndex(nameint("Hl",n),move(inqn));
        //printfln("links[%d]=\n%s",n,links[n]);

        //if(n <= 2 or n == N)
        //    {
        //    println("basis for site ",n);
        //    for(size_t l = 0; l < bn.size(); ++l) printfln("%d %s %s",l,bn.at(l).st,bn.at(l).q);
        //    println();
        //    printfln("IQIndex for site %d:\n%s",n,links.at(n));
        //    }
        }

#ifdef SHOW_AUTOMPO
    static string ws[100][100];
#endif

    //Create arrays indexed by lattice sites.
    //For lattice site "j", ht_by_n[j] contains
    //all HTerms (operator strings) which begin on,
    //end on, or cross site "j"
    auto ht_by_n = vector<vector<HTerm>>(N+1);
    for(auto& ht : am.terms()) 
    for(auto& st : ht.ops)
        {
        ht_by_n.at(st.i).push_back(ht);
        }

    for(auto n : range1(N))
        {
        auto& bn1 = basis.at(n-1);
        auto& bn  = basis.at(n);

        auto& W = H.Aref(n);
        auto &row = links.at(n-1),
             &col = links.at(n);

        W = Tensor(dag(sites(n)),prime(sites(n)),dag(row),col);

        for(auto r : range(row.m()))
        for(auto c : range(col.m()))
            {
            auto& rst = bn1.at(r).st;
            auto& cst = bn.at(c).st;


#ifdef SHOW_AUTOMPO
            ws[r][c] = "0";
#endif
            //auto rc = setElt(dag(row)(r+1)) * setElt(col(c+1));
            auto rc = setElt(dag(row)(r+1),col(c+1));

            //Start a new operator string
            if(cst.i == n && rst == IL)
                {
                //Call startTerm to handle fermionic cases with Jordan-Wigner strings
                auto op = startTerm(cst.op);
                //if(Global::debug1())
                //    {
                //    println("\nAttempting to add the following");
                //    PrintData(sites.op(op,n));
                //    printfln("cst.coef = %f",cst.coef);
                //    PrintData(cst.coef * sites.op(op,n));
                //    auto tmp = cst.coef * sites.op(op,n) * rc;
                //    PrintData(tmp);
                //    PrintData(W);
                //    EXIT
                //    }
                W += sites.op(op,n) * rc;
#ifdef SHOW_AUTOMPO
                ws[r][c] = op;
#endif
                }

            //Add identity "string" connecting operator
            //strings of more than two sites in length
            if(cst == rst)
                {
                /*
                int found = 0;
                for(const auto& ht : ht_by_n.at(n))
                    {
                    if(ht.first() == rst &&
                       ht.first().i != n && 
                       ht.last().i  != n)
                        {
                        for(const auto& st : ht.ops)
                        if(st.i == n)
                            {
                            found += 1;
#ifdef SHOW_AUTOMPO
                            ws[r][c] = format("%.2f %s",st.coef,st.op);
#endif
                            W += st.coef * sites.op(st.op,n) * rc;
                            }
                        }
                    }
                //if(found == 0)
                    */

                if(isFermionic(cst))
                    {
                    W += sites.op("F",n) * rc;
                    }
                else
                    {
                    W += sites.op("Id",n) * rc;
                    }
#ifdef SHOW_AUTOMPO
                if(isFermionic(cst)) ws[r][c] = "F";
                else                 ws[r][c] = "1";
#endif
                //if(found > 1)
                //    {
                //    println("Warning: found > 1 at site ",n);
                //    PAUSE
                //    }
                }

            //End operator strings
            if(cst == HL)
                {
                //Check if operator is an ending operator
                for(const auto& ht : ht_by_n.at(n))
                if(rst == ht.first() && ht.last().i == n)
                    {
                    auto op = endTerm(ht.last().op);
                    W += ht.coef * sites.op(op,n) * rc;
#ifdef SHOW_AUTOMPO
                    ws[r][c] = op;
                    auto coef = ht.coef;
                    if(isApproxReal(coef))
                        {
                        ws[r][c] = format("%.2f %s",coef.real(),op);
                        }
                    else
                        {
                        ws[r][c] = format("%.2f %s",coef,op);
                        }
#endif
                    }
                }

            //Include on-site operators
            if(rst == IL && cst == HL)
                {
                for(const auto& ht : ht_by_n.at(n))
                if(ht.first().i == ht.last().i)
                    {
#ifdef SHOW_AUTOMPO
                    if(isApproxReal(ht.first().coef))
                        ws[r][c] = format("%.2f %s",ht.coef.real(),ht.first().op);
                    else
                        ws[r][c] = format("%.2f %s",ht.coef,ht.first().op);
#endif
                    W += ht.coef * sites.op(ht.first().op,n) * rc;
                    }
                }

            }

#ifdef SHOW_AUTOMPO
        if(n <= 10 or n == N)
            {
            for(int r = 0; r < row.m(); ++r, println())
            for(int c = 0; c < col.m(); ++c)
                {
                print(ws[r][c],"\t");
                if(ws[r][c].length() < 8 && c == 1) 
                print("\t");
                }
            println("=========================================");
            }
#endif
        }

    H.Aref(1) *= setElt(links.at(0)(1));
    H.Aref(N) *= setElt(dag(links.at(N))(1));

    //checkQNs(H);

    return H;
    }

//
// Start of approximate toMPO definitions and functions
//

struct IQMPOMatElem
    {
    QN rowqn, colqn;
    int row, col;
    HTerm val;
    
    IQMPOMatElem() { }

    IQMPOMatElem(const QN &rqn, const QN &cqn, int r, int c, const HTerm &t) : 
        rowqn(rqn), colqn(cqn), row(r), col(c), val(t) {};
        
    bool 
    operator==(const IQMPOMatElem &other) const
        {
        return rowqn == other.rowqn && colqn == other.colqn && 
                row == other.row && col == other.col && 
                val == other.val;
        }

    bool
    operator<(IQMPOMatElem const& o) const
        {
        if(row != o.row)
            {
            return row < o.row;
            }
        else if(col != o.col)
            {
            return col < o.col;
            }
        else if(rowqn != o.rowqn)
            {
            return rowqn < o.rowqn;
            }
        else if(colqn != o.colqn)
            {
            return colqn < o.colqn;
            }
        return val < o.val;
        }
    };
    
struct MatIndex
    {
    int row, col;
    MatIndex(int r, int c) : row(r), col(c) {};
    
    bool operator==(const MatIndex &other) const {return row == other.row && col == other.col; }
    };

struct MatElem
    {
    MatIndex ind;
    Complex val;
    
    MatElem(MatIndex index, Complex v) : ind(index), val(v) {};
    
    bool operator==(const MatElem &other) const {return ind == other.ind && val == other.val; }
    };

CMatrix
toCMatrix(vector<MatElem> const& M)
    {
    CMatrix C;
    int nr = 0, nc = 0;
    
    for(MatElem const& elem : M)
        {
        nr = max(nr,1+elem.ind.row);
        nc = max(nc,1+elem.ind.col);
        }
    
    resize(C,nr,nc);
        
    for(MatElem const& elem : M)
        {
        C(elem.ind.row,elem.ind.col) = elem.val;
        }
    return C;
    }

void 
decomposeTerm(int n, 
              SiteTermProd const& ops, 
              SiteTermProd & left, 
              SiteTermProd & onsite, 
              SiteTermProd & right)
    {
    auto isOnSiteOrOnTheRight = [&n](const SiteTerm &t) {return t.i >= n;};
    auto startOfOnSite = find_if(ops.begin(), ops.end(), isOnSiteOrOnTheRight);
    
    auto isOnTheRight = [&n](const SiteTerm &t) {return t.i > n;};
    auto startOfRightPart = find_if(startOfOnSite, ops.end(), isOnTheRight);

    left = SiteTermProd(ops.begin(), startOfOnSite);
    onsite = SiteTermProd(startOfOnSite, startOfRightPart);
    right = SiteTermProd(startOfRightPart, ops.end());
    }  

struct Block
    {
    using Basis = map<SiteTermProd,int>;
    Basis left;
    Basis right;
    vector<MatElem> mat;        
    };

using QNBlock = map<QN, Block>;
using IQMatEls = set<IQMPOMatElem>;
using MPOMatrix = vector<vector<IQTensor>>;

// Returns a 0-based index of the SiteTermProd ops in the vector
// If ops is not in the vector adds it is added
int
posInBlock(SiteTermProd const& ops, 
           Block::Basis & b)
    {
    auto it = b.find(ops);
    if(it != b.end()) return it->second;
    int i = static_cast<int>(b.size());
    b[ops] = i;
    return i;
    }
    
//
// Construct left & right partials and the 
// coefficients matrix on each link as well as the temporary MPO
//
void
partitionHTerms(SiteSet const& sites,
                AutoMPO::storage const& terms,
                vector<QNBlock> & qbs, 
                vector<IQMatEls> & tempMPO)
    {
    auto N = sites.N();

    //
    // qnmap caches the quantum numbers of various products
    // of operators encountered while building the QNBlock
    // data structures at each bond
    //
    // TODO: The qnmap keys are just strings, which assumes
    //       all operators with the same name have the same
    //       QN divergence. This wouldn't be true e.g. for
    //       a spin model with different spin sizes at 
    //       different sites.
    //
    auto qnmap = map<string,QN>();
    auto calcQN = [&qnmap,&sites](SiteTermProd const& prod)
        {
        QN qn;
        for(auto& st : prod)
            {
            auto it = qnmap.find(st.op);
            if(it != qnmap.end())
                {
                qn += it->second;
                }
            else
                {
                auto Op = sites.op(st.op,st.i);
                auto OpQN = -div(Op);
                qnmap[st.op] = OpQN;
                qn += OpQN;
                }
            }
        return qn;
        };

    qbs.resize(N);
    tempMPO.resize(N);

    for(HTerm const& ht : terms)
    for(int n = ht.first().i; n <= ht.last().i; ++n)
        {
        SiteTermProd left, onsite, right;
        decomposeTerm(n, ht.ops, left, onsite, right);
        
        TIMER_START(10)
        auto lqn = calcQN(left);
        auto sqn = calcQN(onsite);
        TIMER_STOP(10)
        
        TIMER_START(11)
        int j=-1,k=-1;

        // qbs.at(i) are the blocks at the link between sites i+1 and i+2
        // i.e. qbs.at(0) are the blocks at the link between sites 1 and 2
        // and qbs.at(N-2) are the blocks at the link between sites N-1 and N
        // for site n the link on the left is qbs.at(n-2) and the link on the right is part.at(n-1)
        if(left.empty())
            {
            if(not right.empty()) // term starting on site n
                {
                k = posInBlock(right, qbs.at(n-1)[sqn].right);
                }
            }
        else
            {
            auto& leftlink = qbs.at(n-2)[lqn];
            if(right.empty()) // term ending on site n
                {
                j = posInBlock(onsite, leftlink.right);
                }
            else
                {
                j = posInBlock(mult(onsite,right), leftlink.right);
                k = posInBlock(right, qbs.at(n-1)[lqn+sqn].right);
                }
            auto l = posInBlock(left,leftlink.left);
            leftlink.mat.emplace_back(MatIndex(l, j), ht.coef);
            }
            
        // Place the coefficient of the HTerm when the term starts
        Cplx c = (j == -1) ? ht.coef : 1;
        
        bool leftF = isFermionic(left);
        if(onsite.empty())
            {
            if(leftF) onsite.emplace_back("F",n);
            else      onsite.emplace_back("Id",n);
            }
        else
            {
            rewriteFermionic(onsite, leftF);
            }
        TIMER_STOP(11)
        
        //
        // Add only unique IQMPOMatElems to tempMPO
        // TODO: assumes terms are unique I think!
        // 
        TIMER_START(12)
        auto& tn = tempMPO.at(n-1);
        auto el = IQMPOMatElem(lqn, lqn+sqn, j, k, HTerm(c, onsite));

        auto it = tn.find(el);
        if(it == tn.end()) tn.insert(move(el));

        TIMER_STOP(12)
        }

    ////
    //// Debugging printout
    ////
    //for(auto n = 1; n <= N; ++n)
    //    {
    //    printfln("===== tempMPO at %d =====",n);
    //    for(auto& el : tempMPO.at(n-1))
    //        {
    //        printfln("(r,c) = (%d,%d)",el.row,el.col);
    //        printfln("QNs %s %s",el.rowqn,el.colqn);
    //        printfln("HTerm = \n%s",el.val);
    //        println();
    //        }
    //    }

//#ifdef SHOW_AUTOMPO
//    println("Left and Right Partials:");
//    for(unsigned n=0; n<part.size(); n++)
//        {
//        for(const auto &pqn : part.at(n))
//            {
//            const Partition &p = pqn.second;
//
//            println("Left, QN = ", pqn.first);
//            for(const SiteTermProd &prod : p.left)
//                println(prod);
//            println("Right, QN = ", pqn.first);
//            for(const SiteTermProd &prod : p.right)
//                println(prod);
//            println("Coef, QN = ", pqn.first);
//            for(const MatElem &elem : p.mat)
//                println(elem.ind.row,',',elem.ind.col,'\t',elem.val);
//            }
//        println("=========================================");
//        }
//        
//    println();
//    println("TempMPO Elements:");
//    for(unsigned n=0; n<tempMPO.size(); n++)
//        {
//        for(IQMPOMatElem const& elem: tempMPO.at(n))
//            println(elem.rowqn,',',elem.row,'\t',elem.colqn,',',elem.col,'\t',elem.val.coef,'\t',elem.val.ops);
//        println("=========================================");
//        }
//#endif   

    //for(int n = 1; n <= N; ++n)
    //    {
    //    printfln("======== Site %d ==========",n);
    //    printfln("%d QNBlocks",qbs.at(n-1).size());
    //    for(auto& qb : qbs.at(n-1) )
    //        {
    //        auto& qn = qb.first;

    //        println("QN = ",qn);
    //        int l1 = 0,
    //            l2 = 0,
    //            l3 = 0;
    //        int r1 = 0,
    //            r2 = 0,
    //            r3 = 0;
    //        println("Left basis elems:");
    //        for(auto& it : qb.second.left)
    //            {
    //            //printfln("   %s -> %d",it.first,it.second);
    //            if(it.first.size() == 1ul) ++l1;
    //            if(it.first.size() == 2ul) ++l2;
    //            if(it.first.size() == 3ul) ++l3;
    //            }
    //        printfln("  %d 1-ops",l1);
    //        printfln("  %d 2-ops",l2);
    //        printfln("  %d 3-ops",l3);
    //        //printfln("  %d total",l1+l2+l3);
    //        println("Right basis elems:");
    //        for(auto& it : qb.second.right)
    //            {
    //            //printfln("   %s -> %d",it.first,it.second);
    //            if(it.first.size() == 1ul) ++r1;
    //            if(it.first.size() == 2ul) ++r2;
    //            if(it.first.size() == 3ul) ++r3;
    //            }
    //        printfln("  %d 1-ops",r1);
    //        printfln("  %d 2-ops",r2);
    //        printfln("  %d 3-ops",r3);
    //        //printfln("  %d total",r1+r2+r3);

    //        // Convert the block matrix elements to a dense matrix
    //        auto C = ComplexMatrix(qb.second.mat);

    //        printfln("C is %d x %d",C.Re.Nrows(),C.Re.Ncols());
    //        println();
    //        //println("C.Re = \n",C.Re);
    //        //PAUSE
    //        }
    //    }
    }

         

struct QNProd
    {
    QN q;
    SiteTermProd prod;
    QNProd() { }
    QNProd(QN const& qq, SiteTermProd const& pp) : q(qq), prod(pp) { }
    };
bool
operator<(QNProd const& p1, QNProd const& p2)
    {
    if(p1.q != p2.q) return p1.q < p2.q;
    return p1.prod < p2.prod;
    }
using MPOPiece = map<QNProd,CMatrix>;

// SVD the coefficients matrix on each link and construct the compressed MPO matrix
void
compressMPO(SiteSet const& sites,
            vector<QNBlock> const& qbs, 
            vector<IQMatEls> const& tempMPO,
            vector<MPOPiece> & finalMPO, 
            vector<IQIndex> & links, 
            bool isExpH = false, 
            Complex tau = 0,
            Args const& args = Args::global())
    {
    const int N = sites.N();
    Real eps = 1E-14;

    int minm = args.getInt("Minm",1);
    int maxm = args.getInt("Maxm",5000);
    Real cutoff = args.getReal("Cutoff",1E-16);
    //printfln("Using cutoff = %.2E",cutoff);
    //printfln("Using minm = %d",minm);
    //printfln("Using maxm = %d",maxm);

    finalMPO.resize(N);
    links.resize(N+1);
    
    auto V_n = map<QN, CMatrix>();
    
    const QN ZeroQN;
    
    int d0 = isExpH ? 1 : 2;
    
    links.at(0) = IQIndex("Hl0",Index("hl0_0",d0),ZeroQN);

    auto max_d = links.at(0).m();
    for(int n = 1; n <= N; ++n)
        {
        //printfln("=== Making compressed MPO at site %d ===",n);
        //Put in factor of (-tau) if isExpH==true
        if(isExpH) Error("Need to put in factor of (-tau)");

        auto V_npp = map<QN, CMatrix>();

        int nsector = 1; //always have ZeroQN sector

        for(auto& qb : qbs.at(n-1) )
            {
            auto& qn = qb.first;
            if(qn != ZeroQN) ++nsector;

            // Convert the block matrix elements to a dense matrix
            auto C = toCMatrix(qb.second.mat);

            //println("<><><><><><><> Doing SVD: <><><><><><><><><><>");
            //println("qn = ",qn);
            //println("C.Re = \n",C.Re);

            //auto& Vq = V_npp[qn];
            //auto& Vre = Vq.Re;
            //auto& Vim = Vq.Im;
            auto& V = V_npp[qn];

            CMatrix U;
            Vector D;
            SVD(C,U,D,V);

            //square singular vals for call to truncate
            for(auto& d : D) d = sqr(d);
            truncate(D,maxm,minm,cutoff);
            int m = D.size();

            int nc = ncols(C);
            resize(V,nc,m);

            //println("Vre = \n",Vre);
            //println("<><><><><><><><><><><><><><><><><><><><><><><>");
            }

        int count = 0;
        auto inqn = stdx::reserve_vector<IndexQN>(nsector);
        // Make sure zero QN is first in the list of indices
        inqn.emplace_back(Index(format("hl%d_%d",n,count++),d0+ncols(V_npp[ZeroQN])),ZeroQN);        
        for(auto const& qb : qbs.at(n-1))
            {
            QN const& q = qb.first;
            if(q == ZeroQN) continue; // was already taken care of
            int m = ncols(V_npp[q]);
            inqn.emplace_back(Index(format("hl%d_%d",n,count++),m),q);
            }
        links.at(n) = IQIndex(nameint("Hl",n),move(inqn));

        //printfln("()()()() Link size at %d is %d",n,links.at(n).m());

        //
        // Construct the compressed MPO
        //
        auto& fm = finalMPO.at(n-1);

        auto& IdM = fm[QNProd{ZeroQN,SiteTermProd(1,{"Id",n})}];
        IQIndex& ll = links.at(n-1);
        IQIndex& rl = links.at(n);

        Index li = findByQN(ll,ZeroQN);
        Index ri = findByQN(rl,ZeroQN);
        IdM = CMatrix(li.m(),ri.m());
        IdM(0,0) = 1.;
        if(!isExpH) IdM(1,1) = 1.;

        for(IQMPOMatElem const& elem: tempMPO.at(n-1))
            {
            int j = elem.row;
            int k = elem.col;
            auto& t = elem.val;
            
            if(isZero(t.coef,eps)) continue;

            auto& M = fm[QNProd{elem.rowqn,t.ops}];

            if(nrows(M)==0)
                {
                auto li = findByQN(ll,elem.rowqn);
                auto ri = findByQN(rl,elem.colqn);
                M = CMatrix(li.m(),ri.m());
                }

            int rowOffset = isExpH ? 0 : 1;

            //rowShift & colShift account for special identity
            //entries in zero QN block of MPO
            auto rowShift = (elem.rowqn==ZeroQN) ? d0 : 0;
            auto colShift = (elem.colqn==ZeroQN) ? d0 : 0;

            if(j==-1 && k==-1)	// on-site terms
                {
                M(rowOffset,0) += t.coef;
                }
            else if(j==-1)  	// terms starting on site n
                {
                auto& V = V_npp[elem.colqn];
                for(size_t i = 0; i < ncols(V); ++i)
                    {
                    auto z = t.coef*V(k,i);
                    M(rowOffset,i+colShift) += z;
                    }
                }
            else if(k==-1) 	// terms ending on site n
                {
                auto& V = V_n[elem.rowqn];
                for(size_t r = 0; r < ncols(V); ++r)
                    {
                    auto z = t.coef*V(j,r);
                    M(r+rowShift,0) += z;
                    }
                }
            else 
                {
                auto& Vr = V_n[elem.rowqn];
                auto& Vc = V_npp[elem.colqn];
                for(size_t r = 0; r < ncols(Vr); ++r)
                for(size_t c = 0; c < ncols(Vc); ++c) 
                    {
                    auto z = t.coef*Vr(j,r)*Vc(k,c);
                    M(r+rowShift,c+colShift) += z;
                    }
                }
            }

        // Store SVD computed at this step for next link
        V_n = move(V_npp);
        
        max_d = max(max_d, links.at(n).m());
        }
        
    //println("Maximal dimension of the MPO is ", max_d);
    
    ////
    //// Debugging printout
    ////
    //println("Final MPO is:");
    //for(int n=1; n<=N; ++n)
    //    {
    //    println("---------------------------------------------");
    //    println("Site n = ",n);
    //    println("links.at(n-1) = ",links.at(n-1));
    //    println("links.at(n) = ",links.at(n));
    //    for(auto& qnp_m : finalMPO.at(n-1))
    //        {
    //        auto& row = links.at(n-1);
    //        auto& col = links.at(n);

    //        auto rq = qnp_m.first.q;

    //        auto& prod = qnp_m.first.prod;
    //        auto Op = computeProd(sites,prod);
    //        auto sq = div(Op);
    //        auto cq = rq-sq;

    //        auto ri = findByQN(row,rq);
    //        auto ci = findByQN(col,cq);

    //        println("row QN = ",rq);
    //        println("site QN = ",sq);
    //        println("col QN = ",cq);
    //        println("row Index = ",ri);
    //        println("col Index = ",ci);
    //        println("Op = ",qnp_m.first.prod);
    //        println("M.Re = \n",qnp_m.second.Re);
    //        println();
    //        }
    //    println("---------------------------------------------");
    //    //PAUSE
    //    }
    }

IQMPO
constructMPOTensors(SiteSet const& sites,
                    vector<MPOPiece> const& finalMPO, 
                    vector<IQIndex> const& links, 
                    bool isExpH = false)
    {
    IQMPO H(sites);
    int N = sites.N();

    for(int n = 1; n <= N; ++n)
        {
        auto& row = links.at(n-1);
        auto& col = links.at(n);
        auto& W = H.Aref(n);

        W = IQTensor(dag(sites(n)),prime(sites(n)),dag(row),col);

        auto T = IQTensor(dag(row),col);

        //printfln("n = %d finalMPO size = %d",n,finalMPO.at(n-1).size());
        for(auto& qp_M : finalMPO.at(n-1))
            {
            auto rq = qp_M.first.q;
            auto& prod = qp_M.first.prod;
            auto& M = qp_M.second;

            auto Op = computeProd(sites,prod);
            auto sq = div(Op);
            auto cq = rq-sq;
            //-rq + sq + cq == 0
            //==> cq = rq - sq

            auto ri = findByQN(row,rq);
            auto ci = findByQN(col,cq);
            auto t = matrixTensor(M,ri,ci);
            auto TT = T;
            TT += t;
            W += TT*Op;
            W.scaleTo(1.);
            }
        }

//#ifdef DEBUG
//    auto mscale = LogNum(1.);
//    for(int n = 1; n <= N; ++n)
//        {
//        auto& W = H.Aref(n);
//        if(W.scale() > mscale) mscale = W.scale();
//        W.scaleTo(1.);
//        if(div(W) != QN())
//            {
//            Print(n);
//            Print(div(W));
//            PAUSE
//            }
//        }
//    Print(mscale);
//#endif

    int min_n = isExpH ? 1 : 2;
    H.Aref(1) *= setElt(links.at(0)(min_n));
    H.Aref(N) *= setElt(dag(links.at(N))(1));   
    
    return H;
    }

IQMPO
svdIQMPO(AutoMPO const& am, 
         Args const& args)
    {
    bool isExpH = false;
    Cplx tau = 0.;
    auto qbs = vector<QNBlock>();
    auto tempMPO = vector<IQMatEls>();

    //println("Calling partitionHTerms");
    //START_TIMER(1)
    partitionHTerms(am.sites(),am.terms(),qbs,tempMPO);
    //STOP_TIMER(1)

    //return IQMPO();
    
    auto finalMPO = vector<MPOPiece>();
    auto links = vector<IQIndex>();

    //println("Calling compressMPO");
    //START_TIMER(2)
    compressMPO(am.sites(),qbs,tempMPO,finalMPO,links,isExpH,tau,args);
    //STOP_TIMER(2)

    //EXIT
    //return IQMPO();
    
    //println("Calling constructMPOTensors");
    //START_TIMER(3)
    auto H = constructMPOTensors(am.sites(),finalMPO, links);
    //STOP_TIMER(3)

    return H;
    }

template<>
IQMPO 
toMPO(AutoMPO const& am, 
      Args const& args) 
    { 
    if(args.getBool("Exact",false))
        {
        println("Using 'Exact' mode to make MPO");
        return toMPOImpl<IQTensor>(am,args);
        }
    println("Using approx/svd construction of MPO");
    return svdIQMPO(am,args);
    }

template<>
MPO 
toMPO(AutoMPO const& am, 
      Args const& args) 
    { 
    IQMPO res = toMPO<IQTensor>(am,{args,"CheckQN",false});
    return res.toMPO();
    }

//template<>
//MPO
//toMPO<ITensor>(const AutoMPO& a,
//               const Args& args)
//    {
//    auto checkqn = Args("CheckQNs",false);
//    auto res = toMPO<IQTensor>(a,args+checkqn);
//    return res.toMPO();
//    }


IQMPO
toExpH_ZW1(const AutoMPO& am,
           Complex tau,
           const Args& args)
    {
    auto const& sites = am.sites();
    auto H = IQMPO(sites);
    const int N = sites.N();

    for(auto& t : am.terms())
    if(t.Nops() > 2) 
        {
        Error("Only at most 2-operator terms allowed for AutoMPO conversion to MPO/IQMPO");
        }

    //Special SiteTerm objects indicating either
    //a string of identities coming from the first
    //site of the system or the completed Hamitonian
    //for the left-hand side of the system
    SiteTerm IL("IL",0);

    vector<vector<SiteQN>> basis(N+1);
    for(int n = 0; n <= N; ++n)
        basis.at(n).emplace_back(IL,QN());

    //Fill up the basis array at each site with 
    //the unique operator types occurring on the site
    //and starting a string of operators (i.e. first op of an HTerm)
    for(const auto& ht : am.terms())
    for(int n = ht.first().i; n < ht.last().i; ++n)
        {
        auto& bn = basis.at(n);
        auto test = [&ht](const SiteQN& sq){ return sq.st == ht.first(); };
        bool has_first = (std::find_if(bn.cbegin(),bn.cend(),test) != bn.end());
        if(!has_first) 
            {
            auto Op = sites.op(ht.first().op,ht.first().i);
            bn.emplace_back(ht.first(),-div(Op));
            }
        }

    const QN Zero;
    auto qn_comp = [&Zero](const SiteQN& sq1,const SiteQN& sq2)
                   {
                   //First two if statements are to artificially make
                   //the default-constructed Zero QN come first in the sort
                   if(sq1.q == Zero && sq2.q != Zero) return true;
                   else if(sq2.q == Zero && sq1.q != Zero) return false;
                   return sq1.q < sq2.q;
                   };
    //Sort bond "basis" elements by quantum number sector:
    for(auto& bn : basis) std::sort(bn.begin(),bn.end(),qn_comp);

    vector<IQIndex> links(N+1);
    vector<IndexQN> inqn;
    for(int n = 0; n <= N; n++)
        {
        auto& bn = basis.at(n);
        inqn.clear();
        QN currq = bn.front().q;
        int currm = 0;
        int count = 0;
        for(auto& sq : bn)
            {
            if(sq.q == currq)
                {
                ++currm;
                }
            else
                {
                inqn.emplace_back(Index(format("hl%d_%d",n,count++),currm),currq);
                currq = sq.q;
                currm = 1;
                }
            }
        inqn.emplace_back(Index(format("hl%d_%d",n,count++),currm),currq);

        links.at(n) = IQIndex(nameint("Hl",n),move(inqn));

        //if(n <= 2 or n == N)
        //    {
        //    println("basis for site ",n);
        //    for(size_t l = 0; l < bn.size(); ++l) printfln("%d %s %s",l,bn.at(l).st,bn.at(l).q);
        //    println();
        //    printfln("IQIndex for site %d:\n%s",n,links.at(n));
        //    }
        }

#ifdef SHOW_AUTOMPO
    static string ws[100][100];
#endif

    //Create arrays indexed by lattice sites.
    //For lattice site "j", ht_by_n[j] contains
    //all HTerms (operator strings) which begin on,
    //end on, or cross site "j"
    vector<vector<HTerm>> ht_by_n(N+1);
    for(const HTerm& ht : am.terms()) 
    for(const auto& st : ht.ops)
        {
        ht_by_n.at(st.i).push_back(ht);
        }

    for(int n = 1; n <= N; n++)
        {
        auto& bn1 = basis.at(n-1);
        auto& bn  = basis.at(n);

        auto& W = H.Aref(n);
        auto &row = links.at(n-1),
             &col = links.at(n);

        W = IQTensor(dag(sites(n)),prime(sites(n)),dag(row),col);

        for(int r = 0; r < row.m(); ++r)
        for(int c = 0; c < col.m(); ++c)
            {
            auto& rst = bn1.at(r).st;
            auto& cst = bn.at(c).st;

#ifdef SHOW_AUTOMPO
            ws[r][c] = "0";
#endif
            auto rc = setElt(dag(row)(r+1)) * setElt(col(c+1));

            //Start a new operator string
            if(cst.i == n && rst == IL)
                {
#ifdef SHOW_AUTOMPO
                ws[r][c] = format("(-t)*%s",cst.op);
#endif
                auto opname = startTerm(cst.op);
                auto op = sites.op(opname,n) * rc;
                op *= (-tau);
                W += op;
                }

            //Add identity "string" connecting operator
            //strings of more than two sites in length
            if(cst == rst)
                {
#ifdef SHOW_AUTOMPO
                if(isFermionic(cst)) plusAppend(ws[r][c],"F");
                else                 plusAppend(ws[r][c],"1");
#endif
                if(isFermionic(cst))
                    {
                    W += sites.op("F",n) * rc;
                    }
                else
                    {
                    W += sites.op("Id",n) * rc;
                    }
                }

            //End operator strings
            if(cst == IL)
                {
                for(const auto& ht : ht_by_n.at(n))
                if(rst == ht.first() && ht.last().i == n)
                    {
#ifdef SHOW_AUTOMPO
                    ws[r][c] = ht.last().op;
#endif
                    W += ht.coef * sites.op(endTerm(ht.last().op),n) * rc;
                    }
                }

            //Include on-site operators
            if(rst == IL && cst == IL)
                {
                for(const auto& ht : ht_by_n.at(n))
                if(ht.first().i == ht.last().i)
                    {
#ifdef SHOW_AUTOMPO
                    if(isApproxReal(ht.first().coef))
                        plusAppend(ws[r][c],format("(-t*%.2f)*%s",ht.first().coef.real(),ht.first().op));
                    else
                        plusAppend(ws[r][c],format("(-t*%.2f)*%s",ht.first().coef,ht.first().op));
#endif
                    auto op = ht.coef * sites.op(ht.first().op,n) * rc;
                    op *= (-tau);
                    W += op;
                    }
                }

            }

#ifdef SHOW_AUTOMPO
        if(n <= 10 or n == N)
            {
            for(int r = 0; r < row.m(); ++r, println())
            for(int c = 0; c < col.m(); ++c)
                {
                print(ws[r][c],"\t");
                if(ws[r][c].length() < 8 && c == 1) 
                print("\t");
                }
            println("=========================================");
            }
#endif
        }

    H.Aref(1) *= setElt(links.at(0)(1));
    H.Aref(N) *= setElt(dag(links.at(N))(1));

    //checkQNs(H);

    return H;
    }

template<>
IQMPO
toExpH<IQTensor>(const AutoMPO& a,
         Complex tau,
         const Args& args)
    {
    auto approx = args.getString("Approx","ZW1");
    IQMPO res;
    if(approx == "ZW1")
        {
        res = toExpH_ZW1(a,tau,args);
        }
    else
        {
        Error(format("Unknown approximation Approx=\"%s\"",approx));
        }
    return res;
    }

template<>
MPO
toExpH<ITensor>(const AutoMPO& a,
                Complex tau,
                const Args& args)
    {
    IQMPO res = toExpH<IQTensor>(a,tau,args);
    return res.toMPO();
    }

std::ostream& 
operator<<(std::ostream& s, SiteTerm const& t)
    {
    s << t.op << "(" << t.i << ")";
    return s;
    }


std::ostream& 
operator<<(std::ostream& s, HTerm const& t)
    {
    const char* pfix = "";
    if(abs(t.coef-1.0) > 1E-12) 
        {
        s << (isReal(t.coef) ? format("%f ",t.coef.real()) : format("%f ",t.coef));
        }
    for(auto& st : t.ops) 
        {
        s << format("%s%s(%d)",pfix,st.op,st.i);
        pfix = " ";
        }
    return s;
    }

std::ostream& 
operator<<(std::ostream& s, const AutoMPO& a)
    {
    s << "AutoMPO:\n";
    for(const auto& t : a.terms()) s << t << "\n";
    return s;
    }


}
