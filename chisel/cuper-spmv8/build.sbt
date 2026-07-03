ThisBuild / scalaVersion := "2.13.18"
ThisBuild / version := "0.1.0"
ThisBuild / organization := "projectx"

val chiselVersion = "7.11.0"

lazy val root = (project in file("."))
  .settings(
    name := "cuper-spmv8-chisel",
    scalacOptions ++= Seq(
      "-language:reflectiveCalls",
      "-feature",
      "-Xcheckinit",
      "-Ymacro-annotations"
    ),
    addCompilerPlugin("org.chipsalliance" % "chisel-plugin" % chiselVersion cross CrossVersion.full),
    libraryDependencies += "org.chipsalliance" %% "chisel" % chiselVersion
  )
