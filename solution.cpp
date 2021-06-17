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

// type aliases for different id types 
using id_t_ = int;
using sat_id_t = id_t_; 
using user_id_t = id_t_; 

using vector_3d_t = array<float, 3>;
using pos_vec_t = vector<vector_3d_t>;
using scenario_t = map<string, pos_vec_t>;

#define BEAMS_PER_SATELLITE 32 
#define COLORS_PER_SATELLITE 4
#define MAX_USER_VISIBLE_ANGLE 45.0
#define NON_STARLINK_INTERFERENCE_MAX 20.0
#define SELF_INTERFERENCE_MAX 10.0
array<char, (size_t) COLORS_PER_SATELLITE> COLOR_IDS = {'A', 'B', 'C', 'D'};

vector_3d_t ORIGIN = {0,0,0};

struct SatBeamEntry {
	/**
	 * Keep track of a specific sat's color beam usage 
	 */ 
	sat_id_t sat_id; 
	char color;

	// list of beam targets of color
	vector<vector_3d_t>* beam_list;  

	// shared int pointer used to keep track of total beams across 
	// all colors for sat_id 
	int* total_sat_beam_count; 
};

struct UserVisibilityEntry {
	user_id_t user_id; 
	vector<sat_id_t>* visible_sats; 
};

bool sortUsersByPotentialCoverage(UserVisibilityEntry u1, UserVisibilityEntry u2) {
	/**
	 * Sort function for sorting users in ascending order by num of visible sats 
	 * */
	return (*u1.visible_sats).size() < (*u2.visible_sats).size();
}

#define USER_KEY "user"
#define SATS_KEY "sat"
#define INTERFERER_KEY "interferer"

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

float calc_angle(vector_3d_t vertex, vector_3d_t point_a, vector_3d_t point_b)
{
	/**
	 * Returns inner angle formed by (point_a -> vertex) and (point_b -> vertex). 
	 * */

    vector_3d_t va = {point_a[0] - vertex[0], point_a[1] - vertex[1], point_a[2] - vertex[2]};
    vector_3d_t vb = {point_b[0] - vertex[0], point_b[1] - vertex[1], point_b[2] - vertex[2]};

    float va_mag = sqrt( pow(va[0], 2) + pow(va[1], 2) + pow(va[2], 2));
    float vb_mag = sqrt( pow(vb[0], 2) + pow(vb[1], 2) + pow(vb[2], 2));

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

static inline void assign_beams_and_print(scenario_t scenario, 
										  vector<UserVisibilityEntry> user_vis_list, 
										  vector<SatBeamEntry> sat_beam_list) {
	/**
	 * Output the beam assignments to stdout given inputs. Considers each user by traversing
	 * user_vis_list in ascending order and assigns a beam from an availible satellite. 
	 * 
	 * scenario: map regarding user, sat, and interferer locations
	 * user_vis_list: list of users and their visible satellites
	 * sat_beam_list: list of satellites and their currently allocated beams, 
	 * 		s.t. length of sat_beam_list = # sats * # colors ;; (total beams)
	 * 		s.t. sat_beam_list[i + color_index] has data for sat with id i+1 and color COLOR_IDS[color_index]
	 * 			for i in {sat_ids} and color_index in [0, length COLOR_IDS - 1] 
	 * */

	// iterate through users	
	int num_user_entries = (int) user_vis_list.size();
	int num_colors = (int) COLOR_IDS.size();
	for (int i = 0; i < num_user_entries; i ++) {
		// iterate through sats 
		user_id_t user_i = user_vis_list[i].user_id;

		// index in user's visible satellite list 
		int sat_list_i = 0;

		// have we assigned a beam to this user
		bool assigned_beam = false;

		// iterate through all visible satellites for this user
		int num_visible_sats = (*user_vis_list[i].visible_sats).size();
		while (sat_list_i < num_visible_sats && !assigned_beam) {
			sat_id_t sat_i = (*user_vis_list[i].visible_sats)[sat_list_i];
			int sat_beam_i = sat_i * COLORS_PER_SATELLITE; // sat_beam is 0-indexed, sat_id is 1
			SatBeamEntry beam_entry = sat_beam_list[sat_beam_i];

			// see if has beams left to delegate
			if ((*beam_entry.total_sat_beam_count) >= BEAMS_PER_SATELLITE) {
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
				bool self_interference = false;
				int num_existing_beams = (int) current_beam_list.size();
				for (int beam_i = 0; beam_i < num_existing_beams; beam_i ++) {
					vector_3d_t beam_target = current_beam_list[beam_i];
					float self_interfere_angle = calc_angle(sat_pos, user_pos, beam_target);
					if (self_interfere_angle < SELF_INTERFERENCE_MAX) {
						self_interference = true; 
						break;
					}
				}

				// adding a beam to the user for this color is ok
				if (!self_interference) {
					// update the entry for satellite
					(*next_beam_entry.beam_list).push_back(user_pos);

					// update the total for this satellite
					(*next_beam_entry.total_sat_beam_count) += 1;

					// all ids are stored as 0-indexed, so +1 for 1-indexed specs
					cout << "sat " << next_beam_entry.sat_id + 1 << " "; 
					cout << "beam " << *next_beam_entry.total_sat_beam_count << " "; 
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

inline static vector<UserVisibilityEntry> generate_user_vis_list(scenario_t scenario, vector<SatBeamEntry> sat_beam_list) {
	/**
	 * Generates a user_vis_list given the scenario
	 * 
	 * Returns a list of len(# users), where each entry contains a user_id and sats that user 
	 * 	could connect to while observing 1) user visibility constraint and 2) non-starlink interferer constraint. 
	 * */

	vector<UserVisibilityEntry> user_vis_list = {};

	int num_users = (int) scenario[USER_KEY].size();
	int num_sat_beams = (int) sat_beam_list.size();
	int num_interferers = (int) scenario[INTERFERER_KEY].size();

	// below loop would be good for parallelizing
	for (int user_i = 0; user_i < num_users; user_i ++) {
		struct UserVisibilityEntry new_entry = {user_i, new vector<sat_id_t>()};

		// iterate over each satellite using the sat_beam_list vector 
		for (int sat_beam_i = 0; sat_beam_i < num_sat_beams; sat_beam_i += COLORS_PER_SATELLITE) {
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
				continue;
			}

			// if here, sat could form beam w user 
			(*new_entry.visible_sats).push_back(sat_id);
		}

		user_vis_list.push_back(new_entry);
	}

	return user_vis_list;
}

void solve(string filename) {
	/**
	 * Parse scenario at filename 
	 * 
	 * General flow: 
	 * - build scenario object
	 * - build sat_beam_list; create SatBeamEntry for sat {sat_id} for color {all colors}
	 * - build user_vis_list; create UserVisibilityEntry for user {users}, adding 
	 * 							sat in {sat_id} if (visible && !non_starlink_interference)
	 * - sort user_vis_list by coverage 
	 * - assign beams and print solution 
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

	vector<UserVisibilityEntry> user_vis_list = generate_user_vis_list(scenario, sat_beam_list);

	// sort visibility list ascending potential coverage 
	sort(user_vis_list.begin(), user_vis_list.end(), sortUsersByPotentialCoverage);

	assign_beams_and_print(scenario, user_vis_list, sat_beam_list);
    return;
}


int main(int argc, char** argv)
{
	if (argc != 2) {
		cout << "Expected argument: /path/to/scenario.txt" << endl;
		return 0;
	}
    solve(argv[1]);
	return 0;
}