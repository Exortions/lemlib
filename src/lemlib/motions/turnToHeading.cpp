#include "lemlib/motions/turnToHeading.hpp"
#include "lemlib/MotionCancelHelper.hpp"
#include "LemLog/logger/Helper.hpp"
#include "lemlib/Timer.hpp"
#include <optional>

void lemlib::turnToHeading(AngleRange heading, Time timeout, lemlib::TurnToHeadingParams params,
                           lemlib::TurnToHeadingSettings settings) {
    params.minSpeed = abs(params.minSpeed);

    lemlib::MotionCancelHelper helper;
    logger::Helper logger("lemlib/motions/turnToHeading");

    settings.exitConditions.reset();
    settings.angularPID.reset();

    std::optional<Angle> previousRawDeltaTheta = std::nullopt;
    std::optional<Angle> previousDeltaTheta = std::nullopt;
    Angle startingTheta = settings.poseGetter().theta();
    lemlib::Timer timer(timeout);
    Angle targetTheta = heading;
    Angle deltaTheta = 0_stRot;

    bool settling = false;

    double previousMotorPower = 0.0;
    double motorPower = 0.0;

    logger.log(logger::Level::INFO, "Turning to {} deg", heading.convert(deg));

    while (helper.wait(10_msec) && !timer.isDone() && !settings.exitConditions.update(deltaTheta)) {
        // get the robot's current position
        units::Pose pose = settings.poseGetter();

        const Angle rawDeltaTheta = angleError(targetTheta, pose.theta());
        settling = previousRawDeltaTheta && units::sgn(rawDeltaTheta) != units::sgn(previousRawDeltaTheta.value());
        previousRawDeltaTheta = rawDeltaTheta;

        deltaTheta = angleError(targetTheta, pose.theta(), settling ? AngularDirection::AUTO : params.direction);
        if (previousDeltaTheta == std::nullopt) previousDeltaTheta = deltaTheta;

        // motion chaining
        // exit the motion to immediately continue to the next one
        if (params.minSpeed != 0 && units::abs(deltaTheta) < params.earlyExitRange) break;
        if (params.minSpeed != 0 && units::sgn(deltaTheta) != units::sgn(previousDeltaTheta.value())) break;

        // calculate speed
        motorPower = settings.angularPID.update(deltaTheta.convert(deg));

        motorPower = lemlib::respectSpeeds(motorPower, previousMotorPower, params.maxSpeed, params.minSpeed,
                                           units::abs(deltaTheta) > 20_stDeg ? settings.angularPID.getGains().slew : 0);
        previousMotorPower = motorPower;

        logger.log(logger::Level::DEBUG, "Turning with {} power, error: {}", motorPower, deltaTheta.convert(deg));

        // move the motors
        settings.leftMotors.move(motorPower);
        settings.rightMotors.move(-motorPower);
    }

    // stop the drivetrain
    settings.leftMotors.move(0);
    settings.rightMotors.move(0);
}