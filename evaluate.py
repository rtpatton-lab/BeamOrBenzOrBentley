#!/usr/bin/env python3.7
"""
Starlink beam-planning evaluation tool.

Given a scenario and a solution, validates the solution and provides user
experience metrics.
"""
import sys
from collections import namedtuple
from math import sqrt, acos, degrees, floor

# A type for our 3D points.
# In this scenario, units are in km.
Vector3 = namedtuple('Vector3', ['x', 'y', 'z'])

# Center of the earth.
origin = Vector3(0,0,0)

# Speed of light, km/s
speed_of_light_km_s = 299792.0

# Beams per satellite.
beams_per_satellite = 32

# List of valid beam IDs.
valid_beam_ids = [str(i) for i in range(1, beams_per_satellite + 1)]

# Colors per satellite.
colors_per_satellite = 4

# List of valid color IDs.
valid_color_ids = [chr(ord('A') + i) for i in range(0, colors_per_satellite)]

# Self-interference angle, degrees
self_interference_max = 10.0

# Non-Starlink interference angle, degrees
non_starlink_interference_max = 20.0

# Max user to Starlink beam angle, degrees from vertical.
max_user_visible_angle = 45.0


def calculate_angle_degrees(vertex: Vector3, point_a: Vector3, point_b: Vector3) -> float:
    """
    Returns: the angle formed between point_a, the vertex, and point_b in degrees.
    """

    # Calculate vectors va and vb
    va = Vector3(point_a.x - vertex.x, point_a.y - vertex.y, point_a.z - vertex.z)
    vb = Vector3(point_b.x - vertex.x, point_b.y - vertex.y, point_b.z - vertex.z)

    # Calculate each vector's magnitude.
    va_mag = sqrt( (va.x ** 2) + (va.y ** 2) + (va.z ** 2) )
    vb_mag = sqrt( (vb.x ** 2) + (vb.y ** 2) + (vb.z ** 2) )

    # Normalize each vector.
    va_norm = Vector3(va.x / va_mag, va.y / va_mag, va.z / va_mag)
    vb_norm = Vector3(vb.x / vb_mag, vb.y / vb_mag, vb.z / vb_mag)

    # Calculate the dot product.
    dot_product = (va_norm.x * vb_norm.x) + (va_norm.y * vb_norm.y) + (va_norm.z * vb_norm.z)

    # Error can add up here. Bound the dot_product to something we can take the acos of. Scream if it's a big delta.
    dot_product_bound = min(1.0, max(-1.0, dot_product))
    if abs(dot_product_bound - dot_product) > 0.000001:
        print(f"dot_product: {dot_product} bounded to {dot_product_bound}")

    # Return the angle.
    return degrees(acos(dot_product_bound))


def calculate_distance(point_a: Vector3, point_b: Vector3) -> float:
    """
    Returns: the distance between two 3D points.
    """

    # The square root of the difference squared between each compontent.
    x_diff_squared = (point_b.x - point_a.x) ** 2
    y_diff_squared = (point_b.y - point_a.y) ** 2
    z_diff_squared = (point_b.z - point_a.z) ** 2
    return sqrt(x_diff_squared + y_diff_squared + z_diff_squared)


def check_self_interference(scenario: dict, solution: dict) -> bool:
    """
    Given the scenario and the proposed solution, calculate whether any sat has
    a pair of beams with fewer than self_interference_max degrees of separation.

    Returns: Success or failure.
    """

    print("Checking no sat interferes with itself...")

    for sat in solution:
        # Grab the list of beams per sat, and the sat's location.
        beams = solution[sat]
        keys = list(beams.keys())
        sat_loc = scenario['sats'][sat]
        # Iterate over all pairs of beams.
        for i in range(len(beams)):
            for j in range(i+1, len(beams)):
                # Grab the colors of each beam, only check for
                # self interference if they are the same color.
                color_a = beams[keys[i]][1]
                color_b = beams[keys[j]][1]
                if color_a != color_b:
                    continue

                # Grab the locations of each user.
                user_a = beams[keys[i]][0]
                user_b = beams[keys[j]][0]
                user_a_loc = scenario['users'][user_a]
                user_b_loc = scenario['users'][user_b]

                # Calculate angle the sat sees from one user to the other.
                angle = calculate_angle_degrees(sat_loc, user_a_loc, user_b_loc)
                if angle < self_interference_max:
                    # Bail if this pair of beams interfere.
                    print(f"\tSat {sat} beams {keys[i]} and {keys[j]} interfere.")
                    print(f"\t\tBeam angle: {angle} degrees.")
                    return False

    # Looks good!
    print("\tNo satellite self-interferes.")
    return True


def check_interferer_interference(scenario: dict, solution: dict) -> bool:
    """
    Given the scenario and the proposed solution, calculate whether any sat has
    a beam that will interfere with a non-Starlink satellite by placing a beam
    that the user would see as within non_starlink_interference_max of a
    non-Starlink satellite.

    Returns: Success or failure.
    """

    print("Checking no sat interferes with a non-Starlink satellite...")

    for sat in solution:
        # Iterate over users, by way of satellites.
        sat_loc = scenario['sats'][sat]
        for beam in solution[sat]:
            user = solution[sat][beam][0]
            user_loc = scenario['users'][user]
            # Iterate over the non-Starlink satellites.
            for interferer in scenario['interferers']:
                interferer_loc = scenario['interferers'][interferer]

                # Calculate the angle the user sees from the Starlink to the not-Starlink.
                angle = calculate_angle_degrees(user_loc, sat_loc, interferer_loc)

                if angle < non_starlink_interference_max:
                    # Bail if this link is within the interference threshold.
                    print(f"\tSat {sat} beam {beam} interferes with non-Starlink sat {interferer}.")
                    print(f"\t\tAngle of separation: {angle} degrees.")
                    return False

    # Looks good!
    print("\tNo satellite interferes with a non-Starlink satellite!")
    return True


def check_user_coverage(scenario: dict, solution: dict) -> bool:
    """
    Given the scenario and the proposed solution, percentage of users covered
    and verify each covered user is only covered once.

    Returns: Success or failure.
    """

    print("Checking user coverage...")

    # Build list of covered users.
    covered_users = []

    for sat in solution:
        for beam in solution[sat]:
            user  = solution[sat][beam][0]

            # Bail if the user is already covered elsewhere.
            if user in covered_users:
                print(f"\tUser {user} is covered multiple times by solution!")
                return False

            # Otherwise mark the user as covered.
            covered_users.append(user)

    # Report how many users were covered.
    total_users_count = len(scenario['users'])
    covered_users_count = len(covered_users)
    print(f"{(covered_users_count / total_users_count) * 100}% of {total_users_count} total users covered.")
    return True


def check_user_visibility(scenario: dict, solution: dict) -> bool:
    """
    Given the scenario and the proposed solution, calculate whether all users
    can see their assigned satellite.

    Returns: Success or failure.
    """

    print("Checking each user can see their assigned satellite...")

    for sat in solution:
        for beam in solution[sat]:
            user = solution[sat][beam][0]

            # Grab the user and sat's position.
            user_pos = scenario['users'][user]
            sat_pos = scenario['sats'][sat]

            # Get the angle, relative to the user, between the sat and the
            # center of the earth.
            angle = calculate_angle_degrees(user_pos, origin, sat_pos)

            # User terminals are unable to form beams too far off of from vertical.
            if angle <= (180.0-max_user_visible_angle):

                # Elevation is relative to horizon, so subtract 90 degrees
                # to convert from origin-user-sat angle to horizon-user-sat angle.
                elevation = str(angle - 90)
                print(f"\tSat {sat} outside of user {user}'s field of view.")
                print(f"\t\t{elevation} degrees elevation.")
                print(f"\t\t(Min: {90-max_user_visible_angle} degrees elevation.)")

                return False

    # Looks good!
    print("\tAll users' assigned satellites are visible.")
    return True


def read_object(object_type:str, line:str, dest:dict) -> bool:
    """
    Given line, of format 'type id float float float', grabs a Vector3 from the last
    three tokens and puts it into dest[id].

    Returns: Success or failure.
    """
    parts = line.split()
    if parts[0] != object_type or len(parts) != 5:
        print("Invalid line! " + line)
        return False
    else:
        ident = parts[1]
        try:
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
        except:
            print("Can't parse location! " + line)
            return False

        dest[ident] = Vector3(x, y, z)
        return True


def read_scenario(filename:str, scenario:dict) -> bool:
    """
    Given a filename of a scenario file, and a dictionary to populate, populates
    the dictionary with the contents of the file, doing some validation along
    the way.

    Returns: Success or failure.
    """

    print("Reading scenario file " + filename) 

    scenariofile_lines = open(filename).readlines()
    scenario['sats'] = {}
    scenario['users'] = {}
    scenario['interferers'] = {}
    for line in scenariofile_lines:
        if "#" in line:
            # Comment.
            continue

        elif line.strip() == "":
            # Whitespace or empty line.
            continue

        elif "interferer" in line:
            # Read a non-starlink-sat object.
            if not read_object('interferer', line, scenario['interferers']):
                return False

        elif "sat" in line:
            # Read a sat object.
            if not read_object('sat', line, scenario['sats']):
                return False

        elif "user" in line:
            # Read a user object.
            if not read_object('user', line, scenario['users']):
                return False

        else:
            print("Invalid line! " + line)
            return False

    return True


def read_solution(filename:str, scenario:dict, solution:dict) -> bool:
    """
    Given a filename of a solution file (or an empty string to indicate stdin),
    and a dictionary to populate, populates the dictionary with the contents of
    the file, doing some validation against the scenario along the way.

    Returns: Success or failure.
    """
    if filename == "":
        print("Reading solution from stdin.")
        inputfile = sys.stdin
    else:
        print(f"Reading solution file {filename}.")
        inputfile = open(filename)

    slnfile_lines = inputfile.readlines()
    for line in slnfile_lines:
        parts = line.split()

        if "#" in line:
            # Comment. For simplicity, don't support mixed comment and data.
            continue

        elif len(parts) == 0:
            # A blank or whitespace line, continue.
            continue

        elif len(parts) == 8:
            # A valid-looking line, try to parse it.

            if  parts[0] != "sat" or parts[2] != "beam" or parts[4] != "user" or parts[6] != "color":
                # It's missing some component.
                print("Invalid line! " + line)
                return False

            # format should match: 'sat' satid 'beam' beamid 'user' userid 'color' colorid
            sat_id = parts[1]
            beam_id = parts[3]
            user_id = parts[5]
            color_id = parts[7]

            if not sat_id in scenario['sats']:
                print("Referenced an invalid sat id! " + line)
                return False
            if not user_id in scenario['users']:
                print("Referenced an invalid user id! " + line)
                return False
            if not beam_id in valid_beam_ids:
                print("Referenced an invalid beam id! " + line)
                return False
            if not color_id in valid_color_ids:
                print ("Referenced an invalid color! " + line)
                return False

            if not sat_id in solution:
                solution[sat_id] = {}
            if beam_id in solution[sat_id]:
                print("Beam is allocated multiple times! " + line)
                return False
            solution[sat_id][beam_id] = (user_id, color_id)

        else:
            print("Invalid line! " + line)
            return False
    inputfile.close()
    return True


def main() -> int:
    """
    Entry point. Reads inputs, runs checks, outputs stats.

    Returns: exit code.
    """

    # Make sure args are valid.
    if len(sys.argv) != 3 and len(sys.argv) != 2:
        print("Usage: python3.7 evaluate.py /path/to/scenario.txt [/path/to/solution.txt]")
        print("   If the optional /path/to/solution.txt is not provided, stdin will be read.")
        return -1

    # Read and store inputs. Some validation is done here.

    scenario = {}
    # Scenario structure:
    # scenario['sats'][sat_id] = position as a Vector3
    # scenario['users'][user_id] = position as a Vector3
    # scenario['interferers'][interferer_id] = position as a Vector3

    if not read_scenario(sys.argv[1], scenario):
        return -1

    solution = {}
    # Solution structure:
    # solution[satellite_id][beam_id] = user_id

    if len(sys.argv) != 3:
        if not read_solution("", scenario, solution):
            return -1
    else:
        if not read_solution(sys.argv[2], scenario, solution):
            return -1

    # Check constraints.
    if not check_user_coverage(scenario, solution):
        return -1

    if not check_user_visibility(scenario, solution):
        return -1

    if not check_self_interference(scenario, solution):
        return -1

    if not check_interferer_interference(scenario, solution):
        print("Solution contained a beam that could interfere with a non-Starlink satellite.")
        return -1

    print("\nSolution passed all checks!\n")

    # Exit happily.
    return 0


if __name__ == "__main__":
    exit(main())
