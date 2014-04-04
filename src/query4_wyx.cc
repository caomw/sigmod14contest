/*
 * $File: query4_wyx.cc
 * $Date: Fri Apr 04 16:39:16 2014 +0800
 * $Author: Xinyu Zhou <zxytim[at]gmail[dot]com>
 */

#include "query4.h"
#include "data.h"
#include "lib/common.h"
#include "SumEstimator.h"
#include "lib/hash_lib.h"
#include <omp.h>
#include <queue>
#include <algorithm>
#include <set>
#include <cassert>
#include <cmath>
#include <iostream>
#include <fstream>

using namespace std;

namespace {
	struct HeapEle {
		int vtx;
		double centrality;

		HeapEle() {}
		HeapEle(int _vtx, double _centrality) :
			vtx(_vtx), centrality(_centrality) {
			}
		bool operator < (const HeapEle& r) const
		{
			if (centrality == r.centrality)
				return vtx > r.vtx;
			return centrality < r.centrality;
		}
	};
}


void Query4Calculator::bfs_diameter(const std::vector<vector<int>> &g, int source, int &farthest_vtx,
		int &dist_max, vector<bool> &hash) {
	queue<int> q;
	q.push(source);
	hash[source] = true;
	farthest_vtx = source;
	dist_max = 0;
	for (int depth = 0; !q.empty(); depth ++) {
		int qsize = (int)q.size();
		for (int i = 0; i < qsize; i ++) {
			int v0 = q.front(); q.pop();
			FOR_ITR(v1, g[v0]) {
				if (hash[*v1])
					continue;
				hash[*v1] = true;
				farthest_vtx = *v1;
				dist_max = depth + 1;
				q.push(*v1);
			}
		}
	}
}

vector<int> Query4Calculator::work() {
	TotalTimer ttt("q4calculator");
	Timer timer;

	int est_dist_max = 2;  // TODO: this parameter needs tune
	int diameter = -1;
	std::vector<bool> diameter_hash(np), h2(np);
	for (int i = 0; i < (int)np; i ++) {
		if (diameter_hash[i])
			continue;
		int fv, d;
		bfs_diameter(friends, i, fv, d, h2);
		bfs_diameter(friends, fv, fv, d, diameter_hash);
		if (d > diameter)
			diameter = d;
	}

	est_dist_max = max(2, (int)floor(log((double)diameter) / log(2.0) + 0.5));
	est_dist_max = 3;

	vector<bool> noneed(np, false);
	size_t thres = (double)np * 0.5;
	vector<PII> wrong_result_with_person; wrong_result_with_person.reserve(np);
	double cent_kth_upper = 0;
	int sum_bound = 1e9;
	{
		TotalTimer tttt("estimate random");
		if (np > 10000 && k < 20) {
//			RandomChoiceEstimator estimator1(friends, degree, pow(log(np), 0.333) / (20.2 * pow(np, 0.333)));
			RandomChoiceEstimator estimator1(friends, degree, 0.002);
//			estimator1.error();

			auto wrong_result = move(estimator1.result);

			/*
			 *FOR_ITR(index, estimator1.samples)
			 *    exact_s[*index] = wrong_result[*index];			// they are not wrong result
			 */

			REP(i, np)
				wrong_result_with_person.emplace_back(wrong_result[i], i);
			sort(wrong_result_with_person.begin(), wrong_result_with_person.end());
			REPL(i, thres, np)
				noneed[wrong_result_with_person[i].second] = true;

			vector<PDI> some_real_cent;
			int nr_sample = 2 * k;// can be x * k
			int now_index = -1;
			while (true) {
				now_index ++;
				int pid = wrong_result_with_person[now_index].second;
				int s = get_exact_s(pid);
				if (s < 10)
					continue;
				some_real_cent.emplace_back(get_centrality_by_vtx_and_s(pid, s), pid);
				if (some_real_cent.size() == nr_sample) break;
			}

			// find kth largest
			nth_element(some_real_cent.begin(),
					some_real_cent.begin() + nr_sample - k,
					some_real_cent.end());

			cent_kth_upper = some_real_cent[nr_sample - k].first;
			sum_bound = exact_s[some_real_cent[nr_sample - k].second];
		}
	}

	print_debug("Sum bound: %d\n", sum_bound);

	HybridEstimator estimator(friends, degree, est_dist_max, noneed, sum_bound);
	estimated_s = move(estimator.result);
	if (np > 10000 && k < 20)
		REPL(i, thres, np)
			estimated_s[wrong_result_with_person[i].second] = 1e9;


	vector<HeapEle> heap_ele_buf; heap_ele_buf.reserve(np);
	for (int i = 0; i < (int)np; i ++) {
		double centrality = get_centrality_by_vtx_and_s(i, estimated_s[i]);
		heap_ele_buf.emplace_back(i, centrality);
	}

	priority_queue<HeapEle> q(heap_ele_buf.begin(), heap_ele_buf.end());

	// iterate
	vector<int> ans;
	int cnt = 0;
	{
		TotalTimer ttt("iterate q4 heap");		// about 6% of total q4 time
		double last_centrality = 1e100;
		int last_vtx = -1;
		while (!q.empty()) {
			auto he = q.top(); q.pop();
			int vtx = he.vtx;
			double centrality = he.centrality;
			m_assert(centrality <= last_centrality);
			if (centrality == last_centrality && vtx == last_vtx) {
				ans.emplace_back(vtx);
				if ((int)ans.size() == k) {
					print_debug("Real kth: %.10lf, upper: %.10lf\n", centrality, cent_kth_upper);
					break;
				}
			} else {
				cnt ++;
				int s = get_exact_s(vtx);
				// int es = estimated_s[vtx];
				double new_centrality = get_centrality_by_vtx_and_s(vtx, s);
				q.emplace(vtx, new_centrality);
			}

			last_centrality = centrality;
			last_vtx = vtx;
		}
	}

	if (np > 11000) {
		static int print = 0;
		if (print < 3)
			fprintf(stderr, "cnt: %lu/%d/%d/%d/%d\n", np, cnt, k, (int)diameter, (int)est_dist_max);
		print ++;
	}
	return move(ans);
}


void Query4Handler::add_query(int k, const string& s, int index) {
	TotalTimer timer("Q4");
	// build graph
	vector<bool> persons = get_tag_persons_hash(s);

	size_t np = 0;
	vector<vector<int>> friends;
	vector<int> old_pid;

	{
		TotalTimer tt("build graph q4");
		vector<int> new_pid(Data::nperson);
		REP(i, Data::nperson) {
			if (persons[i]) {
				new_pid[i] = np;
				old_pid.push_back(i);
				np ++;
			}
		}
		friends.resize(np);
#pragma omp parallel for schedule(dynamic) num_threads(4)
		REP(i, Data::nperson) {
			if (not persons[i]) continue;
			auto &fs = Data::friends[i];
			FOR_ITR(itr, fs) {
				int pid = itr->pid;
				if (persons[pid])
					friends[new_pid[i]].push_back(new_pid[pid]);
			}
		}
	}
	/*
	 *    {
	 *        TotalTimer tt("build graph q4");
	 *#pragma omp parallel for schedule(static) num_threads(4)
	 *        REP(i, np) {
	 *            auto& fh = Data::friends[persons[i]];		// sorted by id
	 *            FOR_ITR(itr, fh) {
	 *                auto lb_itr = lower_bound(persons.begin(), persons.end(), itr->pid);
	 *                if (lb_itr != persons.end() and *lb_itr == itr->pid) {
	 *                    int v = (int)distance(persons.begin(), lb_itr);
	 *                    friends[i].push_back(v);
	 *                }
	 *            }
	 *
	 *        }
	 *    }
	 */
	// finish building graph

	Query4Calculator worker(friends, k);
	auto now_ans = worker.work();
	FOR_ITR(itr, now_ans)
		*itr = old_pid[*itr];
	ans[index] = move(now_ans);
	if (Data::nperson > 10000)
		continuation->cont();
}


void Query4Handler::work() { }
void Query4Handler::print_result() {
	FOR_ITR(itr, ans) {
		vector<int>& line = *itr;
		for (size_t k = 0; k < line.size(); k ++) {
			if (k) printf(" ");
			printf("%d", line[k]);
		}
		printf("\n");
	}
}

/*
 * vim: syntax=cpp11.doxygen foldmethod=marker
 */
