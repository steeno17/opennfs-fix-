#ifndef _OPENNFS_CAR_H_
#define _OPENNFS_CAR_H_

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace game {

/*
 * Player input for one frame. throttle and brake are 0..1, steer is -1
 * (left) .. +1 (right).
 */
struct CarControls {
	float throttle;
	float brake;
	float steer;
	CarControls() : throttle(0.0f), brake(0.0f), steer(0.0f) {}
};

/*
 * Simple arcade car. All distances are world units, time is seconds.
 *
 * Heading is in degrees and uses the same convention as visualizer::Camera:
 * forward = (cos(h), 0, sin(h)), so 90 faces +z and increasing the heading
 * turns right.
 */
class Car {
public:
	// tuning
	float max_speed;       // forward top speed, units/s
	float max_reverse;     // reverse top speed, units/s
	float engine_accel;    // full-throttle acceleration at standstill
	float brake_decel;     // braking deceleration
	float rolling_resist;  // constant deceleration when coasting
	float air_drag;        // quadratic drag coefficient
	float max_turn_rate;   // deg/s at low speed with full lock

	// state
	float x, y, z;
	float heading;
	float speed;

	Car() :
		max_speed(40.0f), max_reverse(8.0f), engine_accel(14.0f),
		brake_decel(35.0f), rolling_resist(1.5f), air_drag(0.004f),
		max_turn_rate(90.0f),
		x(0.0f), y(0.0f), z(0.0f), heading(90.0f), speed(0.0f),
		_prev_x(0.0f), _prev_z(0.0f) {}

	void place(float px, float py, float pz, float heading_deg) {
		x = _prev_x = px;
		y = py;
		z = _prev_z = pz;
		heading = heading_deg;
		speed = 0.0f;
	}

	float forward_x() const { return cosf(heading * (float) M_PI / 180.0f); }
	float forward_z() const { return sinf(heading * (float) M_PI / 180.0f); }

	// Advance speed, heading and horizontal position by dt seconds.
	void update(float dt, const CarControls &in) {
		// throttle: strongest at standstill, fades to zero at max_speed
		float drive = 0.0f;
		if (in.throttle > 0.0f && speed < max_speed) {
			float room = 1.0f - (speed > 0.0f ? speed / max_speed : 0.0f);
			drive = in.throttle * engine_accel * room;
		}

		// brake pedal brakes while rolling forward and reverses when stopped
		float brake = 0.0f;
		if (in.brake > 0.0f) {
			if (speed > 0.5f) {
				brake = -in.brake * brake_decel;
			} else if (speed > -max_reverse) {
				brake = -in.brake * engine_accel * 0.5f;
			}
		}

		// resistance always opposes motion
		float resist = rolling_resist + air_drag * speed * speed;
		if (speed > 0.0f) {
			resist = -resist;
		} else if (speed < 0.0f) {
			resist = +resist;
		} else {
			resist = 0.0f;
		}

		float prev_speed = speed;
		speed += (drive + brake + resist) * dt;

		// resistance alone must not push the car through zero speed
		if (in.throttle <= 0.0f && in.brake <= 0.0f &&
				((prev_speed > 0.0f && speed < 0.0f) ||
				 (prev_speed < 0.0f && speed > 0.0f))) {
			speed = 0.0f;
		}
		if (speed < -max_reverse) speed = -max_reverse;
		if (speed > max_speed) speed = max_speed;

		// steering: needs speed to bite, and eases off as speed rises
		float grip = speed / 6.0f;
		if (grip > 1.0f) grip = 1.0f;
		if (grip < -1.0f) grip = -1.0f;
		float turn = in.steer * max_turn_rate * grip / (1.0f + 0.04f * fabsf(speed));
		heading += turn * dt;
		if (heading >= 360.0f) heading -= 360.0f;
		if (heading < 0.0f) heading += 360.0f;

		_prev_x = x;
		_prev_z = z;
		x += forward_x() * speed * dt;
		z += forward_z() * speed * dt;
	}

	// Undo this frame's horizontal move (e.g. the car left the terrain).
	void revert_move() {
		x = _prev_x;
		z = _prev_z;
		speed = 0.0f;
	}

private:
	float _prev_x, _prev_z;
};

};

#endif /* _OPENNFS_CAR_H_ */
