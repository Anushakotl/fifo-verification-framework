#include <iostream>
#include <vector>
#include <map>
#include <queue>
#include <string>
#include <random>
#include <functional>
#include <stdexcept>
using namespace std;

class syncFIFO{
    private:
        int *mem;
        int head, tail, count, depth;
        bool injectBug;
    public:
        syncFIFO(const syncFIFO&) = delete;
        syncFIFO& operator=(const syncFIFO&) = delete;

        syncFIFO(int d, bool bug = false) : head(0), tail(0), count(0), depth(d), injectBug(bug){
            mem = new int[depth];
            for(int i = 0; i < depth; i++){
                mem[i] = 0;
            }
        }

        ~syncFIFO(){
            delete[] mem;
        }

        bool full(){ return count == depth; }
        bool empty(){ return count == 0; }
        int occupancy(){ return count; }

        bool push(int data){
            if(full()){
                if(injectBug){
                    mem[tail] = data;
                    tail = (tail + 1) % depth;
                    head = (head + 1) % depth;
                    return true;
                }
                return false;
            }
            mem[tail] = data;
            tail = (tail + 1) % depth;
            count++;
            return true;
        }

        bool pop(int &data){
            if(empty()){
                return false;
            }
            data = mem[head];
            head = (head + 1) % depth;
            count--;
            return true;
        }
};

class Transactions{
    private:
        static int counter;
    protected:
        int unique_id;
    public:
        Transactions(){
            counter++;
            unique_id = counter;
        }
        virtual ~Transactions(){}

        int getId(){ return unique_id; }
        virtual string kind() = 0;
        virtual void print() = 0;
};
int Transactions::counter = 0;

class WriteTxn : public Transactions{
    private:
        int data;
    public:
        WriteTxn(int d){
            data = d;
        }

        string kind() override{
            return "WRITE";
        }

        void print() override{
            cout << "Txn " << getId() << " " << kind() << " data = " << data << endl;
        }

        int getData(){
            return data;
        }
};

class ReadTxn : public Transactions{
    public:
        string kind() override{
            return "READ";
        }

        void print() override{
            cout << "Txn " << getId() << " " << kind() << endl;
        }
};

struct Observed{
    bool isWrite;
    bool accepted;
    int data;
    int occupancyAfter;
};

class Generator{
    private:
        mt19937 rng;
        int writeWeight;
    public:
        Generator(unsigned seed, int wW) : rng(seed), writeWeight(wW) {}

        vector<Transactions*> generate(int n){
            vector<Transactions*> result;
            uniform_int_distribution<int> pick(0, 99);
            uniform_int_distribution<int> dataVal(0, 255);
            for(int i = 0; i < n; i++){
                if(pick(rng) < writeWeight){
                    result.push_back(new WriteTxn(dataVal(rng)));
                }
                else{
                    result.push_back(new ReadTxn());
                }
            }
            return result;
        }
};

class Driver{
    private:
        syncFIFO &fifo;
    public:
        Driver(syncFIFO &f) : fifo(f) {}

        Observed drive(Transactions *t){
            Observed o;
            WriteTxn *w = dynamic_cast<WriteTxn*>(t);
            if(w != nullptr){
                bool ok = fifo.push(w->getData());
                o.isWrite = true;
                o.accepted = ok;
                o.data = w->getData();
                o.occupancyAfter = fifo.occupancy();
            }
            else{
                int x = 0;
                bool ok = fifo.pop(x);
                o.isWrite = false;
                o.accepted = ok;
                o.data = x;
                o.occupancyAfter = fifo.occupancy();
            }
            return o;
        }
};

class Monitor{
    private:
        vector<function<void(const Observed&)>> subscribers;
    public:
        void subscribe(function<void(const Observed&)> f){
            subscribers.push_back(f);
        }

        void notify(const Observed &o){
            for(const auto &callback : subscribers){
                callback(o);
            }
        }
};

class ScoreboardError : public runtime_error{
    public:
        ScoreboardError(const string &msg) : runtime_error(msg) {}
};

template<typename T>
class Scoreboard{
    private:
        queue<T> expected;
        int depth, checks, mismatches;

        void fail(const string &msg){
            mismatches++;
            cout << "MISMATCH: " << msg << endl;
        }
    public:
        Scoreboard(int d) : depth(d), checks(0), mismatches(0) {}

        void check(const Observed &o){
            int size = (int)expected.size();
            checks++;

            if(o.isWrite){
                if(o.accepted){
                    if(size == depth){
                        fail("write accepted but model was full");
                    }
                    else{
                        expected.push(o.data);
                    }
                }
                else{
                    if(size < depth){
                        fail("write rejected but model was not full");
                    }
                }
            }
            else{
                if(o.accepted){
                    if(size == 0){
                        throw ScoreboardError("DUT returned data but reference model is empty");
                    }
                    else{
                        T exp = expected.front();
                        expected.pop();
                        if(exp != o.data){
                            fail("data mismatch");
                        }
                    }
                }
                else{
                    if(size > 0){
                        fail("read rejected but model was not empty");
                    }
                }
            }
        }

        int getChecks(){ return checks; }
        int getMismatches(){ return mismatches; }
};

class Coverage{
    private:
        map<string, int> bins;
        int depth;
        bool havePrev, prevWasWrite;
    public:
        Coverage(int d) : depth(d), havePrev(false), prevWasWrite(false){
            bins["write_ok"] = 0;
            bins["read_ok"] = 0;
            bins["write_when_full"] = 0;
            bins["read_when_empty"] = 0;
            bins["hit_full"] = 0;
            bins["hit_empty"] = 0;
            bins["back_to_back_writes"] = 0;
            bins["back_to_back_reads"] = 0;
        }

        void sample(const Observed &o){
            if(o.isWrite && o.accepted){
                bins["write_ok"]++;
            }
            if(!o.isWrite && o.accepted){
                bins["read_ok"]++;
            }
            if(o.isWrite && !o.accepted){
                bins["write_when_full"]++;
            }
            if(!o.isWrite && !o.accepted){
                bins["read_when_empty"]++;
            }
            if(o.isWrite && o.accepted && o.occupancyAfter == depth){
                bins["hit_full"]++;
            }
            if(!o.isWrite && o.accepted && o.occupancyAfter == 0){
                bins["hit_empty"]++;
            }
            if(havePrev && prevWasWrite && o.isWrite){
                bins["back_to_back_writes"]++;
            }
            if(havePrev && !prevWasWrite && !o.isWrite){
                bins["back_to_back_reads"]++;
            }

            havePrev = true;
            prevWasWrite = o.isWrite;
        }

        void report(){
            for(auto entry : bins){
                cout << "  " << entry.first << ": " << entry.second;
                if(entry.second == 0){
                    cout << "   <-- HOLE";
                }
                cout << endl;
            }
        }
};

class Component{
    protected:
        string name;
    public:
        Component(string n) : name(n) {}
        virtual ~Component() {}
        virtual void build() {}
        virtual void run() {}
        virtual void report() {}
};

class Environment : public Component{
    private:
        int depth, numTxns, writeWeight, errors;
        unsigned seed;
        bool injectBug;

        syncFIFO *fifo;
        Generator *gen;
        Driver *drv;
        Monitor *mon;
        Scoreboard<int> *sb;
        Coverage *cov;
        vector<Transactions*> txns;
    public:
        Environment(int d, unsigned s, int w, int n, bool bug) : Component("env"){
            depth = d;
            seed = s;
            writeWeight = w;
            numTxns = n;
            injectBug = bug;
            errors = 0;
            fifo = nullptr;
            gen = nullptr;
            drv = nullptr;
            mon = nullptr;
            sb = nullptr;
            cov = nullptr;
        }

        ~Environment(){
            for(Transactions *t : txns){
                delete t;
            }
            delete drv;
            delete mon;
            delete sb;
            delete cov;
            delete gen;
            delete fifo;
        }

        void build() override{
            fifo = new syncFIFO(depth, injectBug);
            gen = new Generator(seed, writeWeight);
            drv = new Driver(*fifo);
            mon = new Monitor();
            sb = new Scoreboard<int>(depth);
            cov = new Coverage(depth);

            mon->subscribe([this](const Observed &o){
                try{
                    sb->check(o);
                }
                catch(const ScoreboardError &e){
                    errors++;
                    cout << "SCOREBOARD ERROR: " << e.what() << endl;
                }
            });

            mon->subscribe([this](const Observed &o){
                cov->sample(o);
            });
        }

        void run() override{
            txns = gen->generate(numTxns);
            for(Transactions *t : txns){
                Observed o = drv->drive(t);
                mon->notify(o);
            }
        }

        void report() override{
            cout << "--- Scoreboard ---" << endl;
            cout << "  checks:     " << sb->getChecks() << endl;
            cout << "  mismatches: " << sb->getMismatches() << endl;
            cout << "  errors:     " << errors << endl;
            cout << "--- Coverage ---" << endl;
            cov->report();
            if(sb->getMismatches() == 0 && errors == 0){
                cout << "RESULT: PASS" << endl;
            }
            else{
                cout << "RESULT: FAIL" << endl;
            }
        }
};

void runTest(string label, int depth, unsigned seed, int writeWeight, int n, bool bug){
    cout << "\n===== " << label << " =====" << endl;
    Environment env(depth, seed, writeWeight, n, bug);
    env.build();
    env.run();
    env.report();
}

int main(){
    runTest("1. clean DUT, write 70%", 4, 1, 70, 1000, false);
    runTest("2. buggy DUT, write 70%", 4, 1, 70, 1000, true);
    runTest("3. buggy DUT, write 10%", 4, 1, 10, 1000, true);
    runTest("4. buggy DUT, write 85%", 4, 1, 85, 1000, true);
    return 0;
}