#include <iostream>
#include <vector>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <map>
#include <array>
#include <cmath>
#include <tuple>
#include <cassert>

using namespace std;

using id_t_ = int;
using sat_id_t = id_t_; 
using user_id_t = id_t_; 
using interferer_id_t = id_t_;

using vector_3d_t = array<float, 3>;
using pos_vec_t = vector<vector_3d_t>;
using scenario_t = map<string, pos_vec_t>;

struct SatBeamEntry {
	sat_id_t sat_id; 
	char color;
	vector<vector_3d_t>* beam_list;
	int* beam_i; 
};

struct UserVisibilityEntry {
	user_id_t user_id; 
	vector<sat_id_t>* visible_sats; 
};

bool compareUserVisibilityEntries(UserVisibilityEntry u1, UserVisibilityEntry u2) {
	return (*u1.visible_sats).size() < (*u2.visible_sats).size();
}

#define BEAMS_PER_SATELLITE 32 
#define COLORS_PER_SATELLITE 4
#define MAX_USER_VISIBLE_ANGLE 45.0
#define NON_STARLINK_INTERFERENCE_MAX 20.0
#define SELF_INTERFERENCE_MAX 10.0
#define USER_KEY "user"
#define SATS_KEY "sat"
#define INTERFERER_KEY "interferer"

vector_3d_t ORIGIN = {0,0,0};

array<char, (size_t) COLORS_PER_SATELLITE> COLOR_IDS = {'A', 'B', 'C', 'D'};

#define DOT(a, b) (a[0] * b[0] + a[1] * b[1] + a[2] * b[2])
#define MAG(a) (sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]))

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

float calc_angle(vector_3d_t vertex, vector_3d_t point_a, vector_3d_t point_b)
{
    vector_3d_t va = {point_a[0] - vertex[0], point_a[1] - vertex[1], point_a[2] - vertex[2]};
    vector_3d_t vb = {point_b[0] - vertex[0], point_b[1] - vertex[1], point_b[2] - vertex[2]};

    float va_mag = sqrt( pow(va[0], 2) + pow(va[1], 2) + pow(va[2], 2) );
    float vb_mag = sqrt( pow(vb[0], 2) + pow(vb[1], 2) + pow(vb[2], 2) );

    vector_3d_t va_norm = {va[0] / va_mag, va[1] / va_mag, va[2] / va_mag};
    vector_3d_t vb_norm = {vb[0] / vb_mag, vb[1] / vb_mag, vb[2] / vb_mag};

    float dot_product = (va_norm[0] * vb_norm[0]) + (va_norm[1] * vb_norm[1]) + (va_norm[2] * vb_norm[2]);

    float dot_product_bound = MIN(1.0, MAX(-1.0, dot_product));

    float angle = acos(dot_product_bound) * 180.0 / 3.141592653589793;
	return angle;
}

// below src : https://stackoverflow.com/a/7408245/7363255
vector<string> split(const string &text, char sep) {
	/**
	 * Split a string by the sep given, returning the tokens
	 * */
	vector<string> tokens;
	size_t start = 0, end = 0;
	while ((end = text.find(sep, start)) != string::npos) {
		tokens.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	tokens.push_back(text.substr(start));
	return tokens;
}

static inline void greedy_solver(scenario_t scenario, vector<UserVisibilityEntry> user_vis_list, vector<SatBeamEntry> sat_beam_list) {
	// iterate through users	
	int num_user_entries = (int) user_vis_list.size();
	int num_colors = (int) COLOR_IDS.size();
	for (int i = 0; i < num_user_entries; i ++) {
		// iterate through sats 
		user_id_t user_i = user_vis_list[i].user_id;
		int sat_list_i = 0;
		bool assigned_beam = false;
		int num_visible_sats = (*user_vis_list[i].visible_sats).size();
		while (sat_list_i < num_visible_sats && !assigned_beam) {
			sat_id_t sat_i = (*user_vis_list[i].visible_sats)[sat_list_i];
			int sat_beam_i = sat_i * COLORS_PER_SATELLITE; // sat_beam is 0-indexed, sat_id is 1
			SatBeamEntry beam_entry = sat_beam_list[sat_beam_i];

			// see if has beams left to delegate
			if ((*beam_entry.beam_i) >= BEAMS_PER_SATELLITE) {
				// go to next sat 
                sat_list_i += 1;
				continue;
			}

			// check if sat in user visibility 
			vector_3d_t sat_pos = scenario[SATS_KEY][sat_i]; 
			vector_3d_t user_pos = scenario[USER_KEY][user_i];

			// Constraint: sat must not already be serving a color beam 
			for (int color_i = 0; color_i < num_colors; color_i ++) {
				SatBeamEntry next_beam_entry = sat_beam_list[sat_beam_i + color_i];
				assert(next_beam_entry.sat_id == sat_i);
				vector<vector_3d_t> current_beam_list = *next_beam_entry.beam_list; 
				// iterate over current beams in color, see if any conflict. 
				// if no conflict, good to assign to beam! 
				bool self_conflict = false;
				int num_existing_beams = (int) current_beam_list.size();
				for (int beam_i = 0; beam_i < num_existing_beams; beam_i ++) {
					vector_3d_t beam_target = current_beam_list[beam_i];
					float self_interfere_angle = calc_angle(sat_pos, user_pos, beam_target);
					if (self_interfere_angle < SELF_INTERFERENCE_MAX) {
						self_conflict = true; 
						break;
					}
				}

				if (!self_conflict) {
					(*next_beam_entry.beam_list).push_back(user_pos);
					(*next_beam_entry.beam_i) += 1;
					assert(*next_beam_entry.beam_i <= BEAMS_PER_SATELLITE);
					cout << "sat " << next_beam_entry.sat_id + 1 << " "; 
					cout << "beam " << *next_beam_entry.beam_i << " "; 
					cout << "user " << user_i + 1 << " "; 
					cout << "color " << next_beam_entry.color << endl; 

					assigned_beam = true;
					break; 
				}
			}
			sat_list_i += 1;
		}
	} 
}

void parse_scenario(string filename) {
	/**
	 * Parse a scenario at the specified filename and
	 * return
	 * */
    ifstream scenario_file(filename);
	if (scenario_file.fail()) {
		cout << "File \'" << filename << "\' does not exist" << endl;
		return; 
	}
    string line_buff;
    scenario_t scenario = {{USER_KEY, {}}, {SATS_KEY, {}}, {INTERFERER_KEY, {}}};

	// sat beam list keeps track of each satellite's commited beams 
	// used during constraint checking in solve function
	vector<SatBeamEntry> sat_beam_list = {};

	// parse the scenario, building the scenario map and the sat beam list 
    while (getline (scenario_file, line_buff)) {
        // Output the text from the file
        if (line_buff[0] == '#' || line_buff == "") 
        {
            continue;
        }
        vector<string> parts = split(line_buff, ' ');
        if (parts.size() != 5) {
            cout << "couldn't read line!";
            cout << line_buff;
			return;
        }
        vector_3d_t pos = {stof(parts[2]), stof(parts[3]), stof(parts[4])};
		assert(parts[0] == USER_KEY || parts[0] == SATS_KEY || parts[0] == INTERFERER_KEY);

		// add to scenario map
        scenario[parts[0]].push_back(pos);

		// add sat to sat beam list 
		if (parts[0] == SATS_KEY) {
			int* beam_i_ptr = new int; 
			(*beam_i_ptr) = 0;
			for (int i = 0; i < COLORS_PER_SATELLITE; i ++) {
				sat_id_t id = stoi(parts[1]) - 1;
				char color = COLOR_IDS[i];
				struct SatBeamEntry entry = {id, color, new vector<vector_3d_t>(), beam_i_ptr};
				sat_beam_list.push_back(entry); 
			}
		}
    }
    scenario_file.close();

	vector<UserVisibilityEntry> user_vis_list = {};
	// build user visibility list
	int num_users = (int) scenario[USER_KEY].size();
	int num_sat_beams = (int) sat_beam_list.size();
	int num_interferers = (int) scenario[INTERFERER_KEY].size();
	for (int user_i = 0; user_i < num_users; user_i ++) {
		int sat_beam_i = 0;
		struct UserVisibilityEntry new_entry = {user_i, new vector<sat_id_t>()};

		while (sat_beam_i < num_sat_beams) {
			SatBeamEntry beam_entry = sat_beam_list[sat_beam_i];

			// check if sat in user visibility 
			sat_id_t sat_id = beam_entry.sat_id;

			vector_3d_t sat_pos = scenario[SATS_KEY][sat_id]; 
			vector_3d_t user_pos = scenario[USER_KEY][user_i];

			float user_sat_angle = calc_angle(user_pos, ORIGIN, sat_pos) <= (180.0 - MAX_USER_VISIBLE_ANGLE); 
			// Constraint: sat must be visible to user
			if (user_sat_angle) {
				// sat is outside of range of user 
				// go to next sat 
				sat_beam_i += COLORS_PER_SATELLITE;
				continue;
			}

			// Constraint: angle with user must not be too small w/ interferer
			bool interferer_violation = false;
			for (int int_i = 0; int_i < num_interferers; int_i ++) {
				vector_3d_t int_pos = scenario[INTERFERER_KEY][int_i];
				if (calc_angle(user_pos, int_pos, sat_pos) < NON_STARLINK_INTERFERENCE_MAX) {
					interferer_violation = true;
					break;
				}
			}
			if (interferer_violation) {
				// interferer
				// go to next sat
				sat_beam_i += COLORS_PER_SATELLITE;
				continue;
			}

			// if here, sat could form beam w user 
			(*new_entry.visible_sats).push_back(sat_id);
			sat_beam_i += COLORS_PER_SATELLITE;
		}

		user_vis_list.push_back(new_entry);
	}

	// sort visibility list ascending in satellite 
	sort(user_vis_list.begin(), user_vis_list.end(), compareUserVisibilityEntries);

	// greedy solver based on visibility list
	greedy_solver(scenario, user_vis_list, sat_beam_list);

    return;
}


int main(int argc, char** argv)
{
	if (argc != 2) {
		cout << "Need to put a filename";
		return 0;
	}
    parse_scenario(argv[1]);
	return 0;
}