#include <bits/stdc++.h>
#include <nlohmann/json.hpp>
using namespace std;
using json = nlohmann::json;

struct Task {
    string id, name;
    int priority, duration;
    vector<string> deps;
    long long start = -1, end = -1;
};

struct Compare {
    map<string, Task>* tasks;
    bool operator()(const string& a, const string& b) {
        return (*tasks)[a].priority > (*tasks)[b].priority; // smaller = higher priority
    }
};

map<string, Task> tasks;
map<string, vector<string>> graph;
map<string, int> indeg, doneDeps, critical;
mutex mtx;
condition_variable cv;
bool finished = false;
int completed = 0;
chrono::steady_clock::time_point beginTime;

long long nowMs() {
    return chrono::duration_cast<chrono::milliseconds>(
        chrono::steady_clock::now() - beginTime
    ).count();
}

bool detectCycle() {
    queue<string> q;
    map<string, int> temp = indeg;
    int count = 0;

    for (auto &p : temp)
        if (p.second == 0) q.push(p.first);

    while (!q.empty()) {
        string u = q.front();
        q.pop();
        count++;

        for (string v : graph[u]) {
            temp[v]--;
            if (temp[v] == 0) q.push(v);
        }
    }

    return count != tasks.size();
}

void calcCriticalPath() {
    queue<string> q;
    map<string, int> temp = indeg;

    for (auto &p : temp) {
        if (p.second == 0) {
            q.push(p.first);
            critical[p.first] = tasks[p.first].duration;
        }
    }

    while (!q.empty()) {
        string u = q.front();
        q.pop();

        for (string v : graph[u]) {
            critical[v] = max(critical[v], critical[u] + tasks[v].duration);
            temp[v]--;

            if (temp[v] == 0)
                q.push(v);
        }
    }
}

void worker(int id, priority_queue<string, vector<string>, Compare>& ready) {
    while (true) {
        string cur;

        {
            unique_lock<mutex> lock(mtx);

            cv.wait(lock, [&]() {
                return !ready.empty() || finished;
            });

            if (finished && ready.empty())
                return;

            cur = ready.top();
            ready.pop();

            tasks[cur].start = nowMs();
            cout << "[" << tasks[cur].start << " ms] Worker "
                 << id << " START " << cur << " - "
                 << tasks[cur].name << endl;
        }

        this_thread::sleep_for(chrono::milliseconds(tasks[cur].duration));

        {
            lock_guard<mutex> lock(mtx);

            tasks[cur].end = nowMs();
            cout << "[" << tasks[cur].end << " ms] Worker "
                 << id << " COMPLETE " << cur << endl;

            completed++;

            for (string nxt : graph[cur]) {
                doneDeps[nxt]++;
                if (doneDeps[nxt] == indeg[nxt])
                    ready.push(nxt);
            }

            if (completed == tasks.size())
                finished = true;
        }

        cv.notify_all();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: ./scheduler tasks.json --workers 4\n";
        return 0;
    }

    string file = argv[1];
    int workers = 4;

    for (int i = 2; i < argc; i++) {
        if (string(argv[i]) == "--workers" && i + 1 < argc)
            workers = stoi(argv[++i]);
    }

    ifstream in(file);
    json data;
    in >> data;

    for (auto &x : data) {
        Task t;
        t.id = x["id"];
        t.name = x["name"];
        t.priority = x["priority"];
        t.duration = x["duration_ms"];
        t.deps = x["depends_on"].get<vector<string>>();

        tasks[t.id] = t;
        indeg[t.id] = t.deps.size();
    }

    for (auto &p : tasks) {
        for (string d : p.second.deps)
            graph[d].push_back(p.first);
    }

    if (detectCycle()) {
        cout << "Error: Circular dependency detected!\n";
        return 0;
    }

    calcCriticalPath();

    Compare cmp;
    cmp.tasks = &tasks;
    priority_queue<string, vector<string>, Compare> ready(cmp);

    for (auto &p : tasks)
        if (indeg[p.first] == 0)
            ready.push(p.first);

    beginTime = chrono::steady_clock::now();

    vector<thread> pool;
    for (int i = 1; i <= workers; i++)
        pool.emplace_back(worker, i, ref(ready));

    cv.notify_all();

    for (auto &t : pool)
        t.join();

    long long total = nowMs();
    int criticalLen = 0;

    cout << "\nFinal Report\n";
    cout << "----------------------\n";
    cout << "Total Wall Clock Time: " << total << " ms\n\n";

    for (auto &p : tasks) {
        cout << p.first << " | Start: " << p.second.start
             << " ms | End: " << p.second.end << " ms\n";
        criticalLen = max(criticalLen, critical[p.first]);
    }

    cout << "\nCritical Path Length: " << criticalLen << " ms\n";

    return 0;
}
