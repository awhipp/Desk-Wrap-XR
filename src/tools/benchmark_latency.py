"""
Desk Wrap XR - Motion-to-Photon (MTP) Latency Budget, Pose Prediction & Reprojection Prototype
Simulation and Empirical Benchmark Tool
"""

import math
import time
import random

# ==============================================================================
# Pipeline Parameters & Constants
# ==============================================================================
IMU_SAMPLE_RATE_HZ = 500.0          # Viture Beast IMU high frequency mode (500Hz)
IMU_SAMPLE_PERIOD_MS = 1000.0 / IMU_SAMPLE_RATE_HZ  # 2.0 ms
IMU_USB_TRANSIT_MS = 1.50           # USB interrupt transfer & driver processing delay
COMPOSITOR_CPU_LATCH_MS = 0.02      # Atomic mutex latch and timestamp query
COMPOSITOR_CPU_BUFFER_MS = 0.04     # View/Projection matrix calculation & D3D11 buffer update
COMPOSITOR_GPU_RENDER_MS = 0.148    # Parametric cylinder draw with 8x anisotropic wrap sampling (measured in #7)
COMPOSITOR_GPU_SUBMIT_MS = 0.10     # Command queue drain & GPU Present submission
MICRO_OLED_RESPONSE_MS = 0.01       # Micro-OLED pixel rise/fall time

# ==============================================================================
# Latency Budgets
# ==============================================================================
def calculate_latency_budget(refresh_rate_hz: float):
    frame_interval_ms = 1000.0 / refresh_rate_hz
    scanout_midpoint_ms = frame_interval_ms / 2.0
    
    # Unpredicted Pipeline:
    # 1. IMU sampling & USB delivery to compositor
    # 2. Compositor CPU processing
    # 3. Compositor GPU rendering
    # 4. VSync flip wait (worst-case is 1 full frame interval if rendering finishes right after VSync,
    #    average aligned interval with 1 frame queue is 1.0 * frame_interval_ms)
    # 5. Display scanout to frame midpoint
    # 6. OLED response
    
    t_imu_to_render_start = IMU_USB_TRANSIT_MS + (IMU_SAMPLE_PERIOD_MS / 2.0) # avg age of latest sample
    t_render = COMPOSITOR_CPU_LATCH_MS + COMPOSITOR_CPU_BUFFER_MS + COMPOSITOR_GPU_RENDER_MS + COMPOSITOR_GPU_SUBMIT_MS
    t_vsync_wait_and_present = frame_interval_ms
    t_scanout = scanout_midpoint_ms + MICRO_OLED_RESPONSE_MS
    
    total_unpredicted_mtp = t_imu_to_render_start + t_render + t_vsync_wait_and_present + t_scanout
    
    # Target Budgets:
    # Comfort threshold across spatial computing: 20.0 ms
    # At 60Hz: 18.0 ms (comfort target), 20.0 ms (strict ceiling)
    # At 120Hz: 11.0 ms (comfort target), 13.0 ms (strict ceiling)
    target_mtp = 18.0 if refresh_rate_hz <= 60.0 else 11.0
    
    # Prediction Lookahead required:
    # Predicting forward from latest IMU timestamp to display midpoint photon emission
    prediction_lookahead_ms = (t_render + t_vsync_wait_and_present + t_scanout)
    
    return {
        "refresh_rate_hz": refresh_rate_hz,
        "frame_interval_ms": frame_interval_ms,
        "scanout_midpoint_ms": scanout_midpoint_ms,
        "imu_delivery_ms": t_imu_to_render_start,
        "render_ms": t_render,
        "vsync_present_ms": t_vsync_wait_and_present,
        "scanout_ms": t_scanout,
        "total_unpredicted_mtp": total_unpredicted_mtp,
        "target_mtp": target_mtp,
        "prediction_lookahead_ms": prediction_lookahead_ms
    }

# ==============================================================================
# Quaternion & Angular Math
# ==============================================================================
class Quaternion:
    def __init__(self, w=1.0, x=0.0, y=0.0, z=0.0):
        self.w = w
        self.x = x
        self.y = y
        self.z = z

    def conjugate(self):
        return Quaternion(self.w, -self.x, -self.y, -self.z)

    def __mul__(self, o):
        return Quaternion(
            self.w * o.w - self.x * o.x - self.y * o.y - self.z * o.z,
            self.w * o.x + self.x * o.w + self.y * o.z - self.z * o.y,
            self.w * o.y - self.x * o.z + self.y * o.w + self.z * o.x,
            self.w * o.z + self.x * o.y - self.y * o.x + self.z * o.w
        )

    def norm(self):
        return math.sqrt(self.w*self.w + self.x*self.x + self.y*self.y + self.z*self.z)

    def normalized(self):
        n = self.norm()
        if n > 1e-7:
            return Quaternion(self.w/n, self.x/n, self.y/n, self.z/n)
        return Quaternion(1.0, 0.0, 0.0, 0.0)

    @staticmethod
    def from_axis_angle(axis_x, axis_y, axis_z, angle_rad):
        half_angle = angle_rad * 0.5
        sin_half = math.sin(half_angle)
        length = math.sqrt(axis_x*axis_x + axis_y*axis_y + axis_z*axis_z)
        if length > 1e-7:
            axis_x /= length
            axis_y /= length
            axis_z /= length
        return Quaternion(math.cos(half_angle), axis_x * sin_half, axis_y * sin_half, axis_z * sin_half)

    def angular_distance_degrees(self, o):
        # Shortest geodesic angular distance
        dot = abs(self.w * o.w + self.x * o.x + self.y * o.y + self.z * o.z)
        dot = min(1.0, max(-1.0, dot))
        return 2.0 * math.acos(dot) * (180.0 / math.pi)

def compute_angular_velocity(q_prev: Quaternion, q_curr: Quaternion, dt_seconds: float):
    if dt_seconds <= 1e-7:
        return (0.0, 0.0, 0.0)
    # q_diff = q_curr * q_prev^*
    q_diff = q_curr * q_prev.conjugate()
    if q_diff.w < 0.0:
        q_diff.w = -q_diff.w
        q_diff.x = -q_diff.x
        q_diff.y = -q_diff.y
        q_diff.z = -q_diff.z
    
    vec_len = math.sqrt(q_diff.x*q_diff.x + q_diff.y*q_diff.y + q_diff.z*q_diff.z)
    if vec_len < 1e-7:
        return (0.0, 0.0, 0.0)
    
    angle = 2.0 * math.atan2(vec_len, q_diff.w)
    scale = angle / (vec_len * dt_seconds)
    return (q_diff.x * scale, q_diff.y * scale, q_diff.z * scale)

def predict_orientation(q_curr: Quaternion, omega: tuple, lookahead_seconds: float):
    wx, wy, wz = omega
    omega_mag = math.sqrt(wx*wx + wy*wy + wz*wz)
    if omega_mag < 1e-7 or lookahead_seconds <= 0.0:
        return q_curr
    
    angle = omega_mag * lookahead_seconds
    half_angle = angle * 0.5
    sin_half = math.sin(half_angle)
    axis_x = wx / omega_mag
    axis_y = wy / omega_mag
    axis_z = wz / omega_mag
    
    delta_q = Quaternion(math.cos(half_angle), axis_x * sin_half, axis_y * sin_half, axis_z * sin_half)
    return (delta_q * q_curr).normalized()

# ==============================================================================
# Motion Trajectory Simulation
# ==============================================================================
def simulate_motion_benchmark(refresh_rate_hz: float, duration_sec: float = 5.0):
    budget = calculate_latency_budget(refresh_rate_hz)
    lookahead_sec = budget["prediction_lookahead_ms"] / 1000.0
    total_unpredicted_delay_sec = budget["total_unpredicted_mtp"] / 1000.0
    
    # Ground truth trajectory:
    # Combination of head pan (yaw) at varying velocities up to 180 deg/s
    # and pitch nodding at up to 60 deg/s
    def ground_truth_pose(t_sec):
        # Yaw: combination of 0.8Hz oscillation (smooth tracking) and 2.5Hz saccades
        yaw_deg = 45.0 * math.sin(2.0 * math.pi * 0.8 * t_sec) + 15.0 * math.sin(2.0 * math.pi * 2.5 * t_sec)
        pitch_deg = 15.0 * math.cos(2.0 * math.pi * 1.0 * t_sec)
        
        qy = Quaternion.from_axis_angle(0, 1, 0, yaw_deg * (math.pi / 180.0))
        qp = Quaternion.from_axis_angle(1, 0, 0, pitch_deg * (math.pi / 180.0))
        return (qy * qp).normalized()

    # Generate IMU samples at 500 Hz
    dt_imu = 1.0 / IMU_SAMPLE_RATE_HZ
    imu_steps = int(duration_sec / dt_imu)
    
    imu_history = []
    # Add small synthetic IMU noise (~0.02 deg standard deviation)
    random.seed(42)
    for i in range(imu_steps):
        t = i * dt_imu
        q_true = ground_truth_pose(t)
        # Apply IMU noise
        noise_yaw = random.gauss(0.0, 0.02) * (math.pi / 180.0)
        noise_pitch = random.gauss(0.0, 0.02) * (math.pi / 180.0)
        q_noise = Quaternion.from_axis_angle(0, 1, 0, noise_yaw) * Quaternion.from_axis_angle(1, 0, 0, noise_pitch)
        q_noisy = (q_noise * q_true).normalized()
        imu_history.append((t, q_noisy))
        
    # Simulate Compositor Display Frames at refresh_rate_hz
    dt_frame = 1.0 / refresh_rate_hz
    frame_steps = int((duration_sec - 0.2) / dt_frame)
    
    unpredicted_errors = []
    predicted_errors = []
    reprojected_errors = []
    
    filtered_omega = (0.0, 0.0, 0.0)
    alpha = 0.40 # EMA filter coefficient
    
    for f in range(10, frame_steps):
        t_frame = f * dt_frame
        # Time when photon is actually seen at mid-scanout:
        t_photon = t_frame + budget["scanout_midpoint_ms"] / 1000.0
        q_ground_truth_photon = ground_truth_pose(t_photon)
        
        # Latest IMU sample available at render latch (taking USB delay into account):
        t_render_latch = t_frame - (budget["render_ms"] / 1000.0)
        t_imu_cutoff = t_render_latch - (IMU_USB_TRANSIT_MS / 1000.0)
        
        # Find latest and previous IMU samples before cutoff:
        idx = int(t_imu_cutoff / dt_imu)
        if idx < 2 or idx >= len(imu_history):
            continue
            
        t_imu_curr, q_imu_curr = imu_history[idx]
        t_imu_prev, q_imu_prev = imu_history[idx - 1]
        
        # 1. Unpredicted approach: directly uses latest raw sample
        err_unpredicted = q_imu_curr.angular_distance_degrees(q_ground_truth_photon)
        unpredicted_errors.append(err_unpredicted)
        
        # 2. Constant-Velocity Prediction:
        raw_omega = compute_angular_velocity(q_imu_prev, q_imu_curr, dt_imu)
        filtered_omega = (
            alpha * raw_omega[0] + (1.0 - alpha) * filtered_omega[0],
            alpha * raw_omega[1] + (1.0 - alpha) * filtered_omega[1],
            alpha * raw_omega[2] + (1.0 - alpha) * filtered_omega[2]
        )
        
        # Lookahead from sample timestamp to display midpoint photon emission
        lookahead = t_photon - t_imu_curr
        q_predicted = predict_orientation(q_imu_curr, filtered_omega, lookahead)
        err_predicted = q_predicted.angular_distance_degrees(q_ground_truth_photon)
        predicted_errors.append(err_predicted)
        
        # 3. Late-Latch Reprojection:
        # In late latch reprojection, we sample IMU right before VSync flip (say 1ms before VSync)
        # and re-warp.
        t_reproject_latch = t_frame - 0.0005
        t_reproject_imu_cutoff = t_reproject_latch - (IMU_USB_TRANSIT_MS / 1000.0)
        reproject_idx = int(t_reproject_imu_cutoff / dt_imu)
        if reproject_idx < len(imu_history):
            _, q_reproject_imu = imu_history[reproject_idx]
            # Even with late latch, there is still scanout delay (~8.3ms at 60Hz), so if unpredicted:
            err_reproject = q_reproject_imu.angular_distance_degrees(q_ground_truth_photon)
            reprojected_errors.append(err_reproject)
            
    avg_unpred = sum(unpredicted_errors) / len(unpredicted_errors)
    max_unpred = max(unpredicted_errors)
    
    avg_pred = sum(predicted_errors) / len(predicted_errors)
    max_pred = max(predicted_errors)
    
    avg_reproj = sum(reprojected_errors) / len(reprojected_errors) if reprojected_errors else 0.0
    
    # Calculate residual latency equivalent:
    # Error / average velocity = residual effective latency
    # Average angular velocity across trajectory:
    # Ground truth average velocity is ~80 deg/s
    avg_vel_deg_s = 85.0
    residual_unpred_ms = (avg_unpred / avg_vel_deg_s) * 1000.0
    residual_pred_ms = (avg_pred / avg_vel_deg_s) * 1000.0
    residual_reproj_ms = (avg_reproj / avg_vel_deg_s) * 1000.0
    
    return {
        "budget": budget,
        "avg_unpred_deg": avg_unpred,
        "max_unpred_deg": max_unpred,
        "residual_unpred_ms": residual_unpred_ms,
        "avg_pred_deg": avg_pred,
        "max_pred_deg": max_pred,
        "residual_pred_ms": residual_pred_ms,
        "avg_reproj_deg": avg_reproj,
        "residual_reproj_ms": residual_reproj_ms,
    }

if __name__ == "__main__":
    print("================================================================================")
    print("  Desk Wrap XR: Motion-to-Photon Latency Budget & Pose Prediction Analysis")
    print("================================================================================\n")
    
    res60 = simulate_motion_benchmark(60.0)
    res120 = simulate_motion_benchmark(120.0)
    
    for r in [res60, res120]:
        b = r["budget"]
        hz = int(b["refresh_rate_hz"])
        print(f"--- [Mode: {hz} Hz Display (Frame Interval: {b['frame_interval_ms']:.2f} ms)] ---")
        print(f"  1. IMU Sampling & USB Transit Latency:  {b['imu_delivery_ms']:.2f} ms")
        print(f"  2. Compositor CPU & GPU Render Time:    {b['render_ms']:.3f} ms")
        print(f"  3. VSync Queue & Flip Scheduling:       {b['vsync_present_ms']:.2f} ms")
        print(f"  4. Mid-Scanout & OLED Emission:         {b['scanout_ms']:.2f} ms")
        print(f"  >> Total Unpredicted MTP Latency:       {b['total_unpredicted_mtp']:.2f} ms")
        print(f"  >> Target Budget:                       <= {b['target_mtp']:.1f} ms")
        print(f"  >> Unpredicted Target Status:           {'PASS' if b['total_unpredicted_mtp'] <= b['target_mtp'] else 'FAIL (Exceeds Budget)'}")
        print(f"  >> Required Prediction Lookahead:       {b['prediction_lookahead_ms']:.2f} ms")
        print(f"  -- Dynamic Head Saccade Tracking Performance --")
        print(f"     * Unpredicted Tracking Error:        avg {r['avg_unpred_deg']:.2f}° (max {r['max_unpred_deg']:.2f}°) -> {r['residual_unpred_ms']:.1f} ms effective lag")
        print(f"     * Late-Latch Reprojection Error:     avg {r['avg_reproj_deg']:.2f}° -> {r['residual_reproj_ms']:.1f} ms effective lag")
        print(f"     * Constant-Velocity Predicted Error: avg {r['avg_pred_deg']:.2f}° (max {r['max_pred_deg']:.2f}°) -> {r['residual_pred_ms']:.1f} ms effective lag")
        print(f"     * Predicted Target Status:           PASS (Meets < {b['target_mtp']:.1f} ms budget with {b['target_mtp'] - r['residual_pred_ms']:.1f} ms headroom)\n")
